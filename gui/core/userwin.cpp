// gui/core/userwin.cpp — janelas criadas por processos Ring 3
// (SYS_WIN_*). Ver gui/core/userwin.h para o modelo geral.
//
// ---- Concorrência (leia antes de mexer) ----------------------------
// Há duas "linhas de execução" tocando em uma UserWindow:
//
//   * a task da GUI (task 0): draw(), on_event() e o fechamento da
//     janela (wm_close -> delete). Roda com interrupções ligadas e
//     PODE ser preemptada pelo timer a qualquer instante;
//   * a task do processo dono, DENTRO de uma syscall. As syscalls
//     entram com IF=0 (SFMASK) e, fora SYS_WIN_EVENT (que cede a CPU
//     em laço), nunca cedem a CPU — logo cada syscall é atômica em
//     relação à GUI.
//
// Regras que isto impõe:
//   1. A syscall NUNCA destrói a janela. A GUI pode estar no meio do
//      draw() dela (preemptada); deletar por baixo seria um
//      use-after-free. SYS_WIN_DESTROY só marca `owner_closed`; a
//      GUI fecha de fato em userwin_reap().
//   2. Quem troca o canvas (apply_resize) ou desmonta a janela
//      (destrutor) faz a troca de ponteiro+dimensões sob IrqOff, para
//      que uma syscall nunca veja ponteiro novo com dimensões antigas
//      (escrita fora do buffer).
//   3. O processo só guarda um HANDLE inteiro, nunca um ponteiro: cada
//      syscall refaz o lookup — inclusive a cada volta do laço de
//      SYS_WIN_EVENT, porque a janela pode ser fechada pelo usuário
//      enquanto o processo está cedendo a CPU.
//   4. Fila de eventos é SPSC (GUI produz, processo consome) sem
//      lock: o produtor escreve o slot e só DEPOIS publica `ev_head`;
//      o consumidor lê o slot e só DEPOIS publica `ev_tail`.
//   5. userwin_create chama wm_register() de dentro de uma syscall —
//      a primeira vez que o WM é alterado por outra task que não a da
//      GUI. É seguro hoje porque a syscall é atômica (IF=0) e o WM só
//      lê s_windows[] slot a slot (um slot ainda nulo/já preenchido é
//      sempre um estado válido para ele). Qualquer OUTRA mutação do WM
//      a partir de syscall (fechar, mover, focar...) deve passar por
//      wm_defer() ou por uma marca consumida em userwin_reap(), como
//      já é feito com SYS_WIN_DESTROY.

#include "userwin.h"
#include "wm.h"
#include "window.h"
#include "../elements/taskbar.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "theme.h"

// Definido em kernel/task.c (C). Declarado aqui para não incluir
// task.h (e seu TASK_LAYOUT_GUARD) numa unidade C++ que não precisa
// do layout de task_t.
// Retorna int (0/1) — em C o `bool` do kernel é `typedef int` (types.h),
// em C++ é o bool nativo; declarar int aqui evita depender disso.
extern "C" int task_is_alive(uint64_t id);

namespace {

const uint32_t USERWIN_EVQ   = 64;         // slots da fila de eventos
const uint32_t USERWIN_BG    = 0x0F1220;   // = COLOR_WIN_BG: fundo inicial do canvas
const uint32_t USERWIN_MIN_W = 64;
const uint32_t USERWIN_MIN_H = 48;

inline uint32_t outer_w_for(uint32_t cw) { return cw + 2 * Window::BORDER; }
inline uint32_t outer_h_for(uint32_t ch) { return ch + Window::BORDER + Window::TITLE_BAR_H + 1; }
inline uint32_t umin(uint32_t a, uint32_t b) { return a < b ? a : b; }

// Desliga interrupções no escopo e restaura o estado anterior de IF.
struct IrqOff {
    uint64_t flags;
    IrqOff()  { __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory"); }
    ~IrqOff() { if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory"); }
};

inline void compiler_barrier() { __asm__ volatile("" ::: "memory"); }

} // namespace

class UserWindow : public Window {
public:
    UserWindow(int32_t x, int32_t y, uint32_t outer_w, uint32_t outer_h,
               const char* title, uint64_t owner,
               uint32_t* buf, uint32_t cw, uint32_t ch)
        : Window(x, y, outer_w, outer_h, title, WinType::Generic),
          canvas(buf), canvas_w(cw), canvas_h(ch),
          owner_id(owner), handle(-1), owner_closed(false),
          ev_head(0), ev_tail(0), last_mx(-100000), last_my(-100000), last_drag(0) {}

    ~UserWindow() override;

    void        draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    // ---- Lado "GUI" ----
    void apply_resize(uint32_t nw, uint32_t nh);
    void push_event(uint32_t type, int32_t a, int32_t b, int32_t c);

    // ---- Lado "processo" (dentro de syscalls) ----
    bool pop_event(haos_win_event_t* out);

    uint32_t*          canvas;       // 0xRRGGBB, canvas_w * canvas_h pixels
    uint32_t           canvas_w;
    uint32_t           canvas_h;
    uint64_t           owner_id;
    int32_t            handle;       // índice em s_win; -1 enquanto não registrada
    volatile bool      owner_closed; // pedido de fechamento (SYS_WIN_DESTROY)

private:
    haos_win_event_t   ev[USERWIN_EVQ];
    volatile uint32_t  ev_head;      // próximo slot a escrever (produtor: GUI)
    volatile uint32_t  ev_tail;      // próximo slot a ler     (consumidor: syscall)
    int32_t            last_mx, last_my;
    int32_t            last_drag;
};

// Tabela handle -> janela. Só contém janelas já registradas no WM.
static UserWindow* s_win[USERWIN_MAX] = { nullptr };

UserWindow::~UserWindow() {
    uint32_t* buf;
    {
        IrqOff off;
        if (handle >= 0 && handle < (int32_t)USERWIN_MAX && s_win[handle] == this)
            s_win[handle] = nullptr;
        buf      = canvas;
        canvas   = nullptr;
        canvas_w = 0;
        canvas_h = 0;
    }
    if (buf) kfree(buf);
}

void UserWindow::push_event(uint32_t type, int32_t a, int32_t b, int32_t c) {
    uint32_t h    = ev_head;
    uint32_t next = (h + 1) % USERWIN_EVQ;
    if (next == ev_tail) return;          // fila cheia: descarta o evento novo
    ev[h].type = type;
    ev[h].a = a;
    ev[h].b = b;
    ev[h].c = c;
    compiler_barrier();                   // slot escrito ANTES de publicar head
    ev_head = next;
}

bool UserWindow::pop_event(haos_win_event_t* out) {
    uint32_t t = ev_tail;
    if (t == ev_head) return false;
    *out = ev[t];
    compiler_barrier();                   // slot lido ANTES de liberar o tail
    ev_tail = (t + 1) % USERWIN_EVQ;
    return true;
}

// Recria o canvas com o novo tamanho da área cliente (janela
// redimensionada/maximizada/encaixada), preservando o que couber para
// não piscar em branco até o programa redesenhar. Só roda na task da
// GUI, dentro de draw().
void UserWindow::apply_resize(uint32_t nw, uint32_t nh) {
    if (nw == 0 || nh == 0 || nw > USERWIN_MAX_DIM || nh > USERWIN_MAX_DIM) return;

    uint32_t* nb = (uint32_t*)kzalloc((size_t)nw * nh * sizeof(uint32_t));
    if (!nb) return;                      // sem memória: mantém o canvas antigo (será recortado)

    for (uint32_t i = 0; i < nw * nh; i++) nb[i] = USERWIN_BG;

    uint32_t cw = umin(nw, canvas_w);
    uint32_t ch = umin(nh, canvas_h);
    if (canvas) {
        for (uint32_t r = 0; r < ch; r++)
            kmemcpy(nb + (size_t)r * nw, canvas + (size_t)r * canvas_w, cw * sizeof(uint32_t));
    }

    uint32_t* old;
    {
        IrqOff off;                       // ponteiro + dimensões trocam juntos (regra 2)
        old      = canvas;
        canvas   = nb;
        canvas_w = nw;
        canvas_h = nh;
    }
    if (old) kfree(old);

    push_event(HAOS_EV_RESIZE, (int32_t)nw, (int32_t)nh, 0);
}

void UserWindow::draw(int32_t ox, int32_t oy) {
    Window::draw(ox, oy);                 // moldura, barra de título, fundo
    if (!active || minimized || owner_closed || !canvas) return;

    Rect c = content_area_absolute();
    if (c.x < 0 || c.y < 0 || c.w == 0 || c.h == 0) return;

    // Área cliente mudou de tamanho (resize/maximizar/snap): recria o
    // canvas. Durante o arrasto de redimensionar (`resizing`) esperamos
    // soltar o mouse — alocar a cada frame fragmentaria o heap.
    if (!resizing && (c.w != canvas_w || c.h != canvas_h))
        apply_resize(c.w, c.h);

    uint32_t w = umin(canvas_w, c.w);
    uint32_t h = umin(canvas_h, c.h);
    fb_blit((uint32_t)c.x, (uint32_t)c.y, w, h, canvas, canvas_w);
}

EventResult UserWindow::on_event(const WidgetEvent& e) {
    if (owner_closed) return EventResult::Ignored;

    // O WM entrega mouse em coordenadas ABSOLUTAS de tela; o programa
    // recebe relativas ao canto da área cliente (onde está o canvas).
    Rect c = content_area_absolute();
    int32_t rx = e.x - c.x;
    int32_t ry = e.y - c.y;

    switch (e.type) {
        case EventType::KeyDown:
            push_event(HAOS_EV_KEY, (int32_t)e.key, 0, 0);
            return EventResult::Handled;

        case EventType::MouseDown:
            push_event(HAOS_EV_MOUSE_DOWN, rx, ry, 0);
            return EventResult::Handled;

        case EventType::MouseUp:
            push_event(HAOS_EV_MOUSE_UP, rx, ry, 0);
            return EventResult::Handled;

        case EventType::MouseMove:
        case EventType::MouseDrag: {
            int32_t drag = (e.type == EventType::MouseDrag) ? 1 : 0;
            // O WM repassa o movimento todo frame; só enfileira se algo mudou.
            if (rx == last_mx && ry == last_my && drag == last_drag)
                return EventResult::Handled;
            last_mx = rx; last_my = ry; last_drag = drag;
            push_event(HAOS_EV_MOUSE_MOVE, rx, ry, drag);
            return EventResult::Handled;
        }

        case EventType::MouseScroll:
            push_event(HAOS_EV_MOUSE_SCROLL, rx, ry, e.scroll);
            return EventResult::Handled;

        default:
            return EventResult::Ignored;
    }
}

// ============================================================
//  API usada pelas syscalls (extern "C")
// ============================================================

// Janela do processo `owner`, ainda aberta — ou nullptr. NUNCA guarde
// o ponteiro devolvido através de uma chamada que possa ceder a CPU.
static UserWindow* lookup(uint64_t owner, int64_t h) {
    if (h < 0 || h >= (int64_t)USERWIN_MAX) return nullptr;
    UserWindow* w = s_win[h];
    if (!w || w->owner_id != owner || w->owner_closed) return nullptr;
    return w;
}

extern "C" int64_t userwin_create(uint64_t owner, uint32_t cw, uint32_t ch, const char* title) {
    uint32_t sw = fb_width();
    uint32_t sh = fb_height();

    // Ajusta a área cliente: mínimo útil .. o que cabe na tela (sem a taskbar).
    uint32_t max_cw = sw - 2 * Window::BORDER;
    uint32_t max_ch = sh - TASKBAR_H - Window::BORDER - Window::TITLE_BAR_H - 1;
    if (cw < USERWIN_MIN_W) cw = USERWIN_MIN_W;
    if (ch < USERWIN_MIN_H) ch = USERWIN_MIN_H;
    if (cw > max_cw) cw = max_cw;
    if (ch > max_ch) ch = max_ch;

    int slot = -1;
    int mine = 0;
    for (int i = 0; i < (int)USERWIN_MAX; i++) {
        UserWindow* w = s_win[i];
        if (!w) { if (slot < 0) slot = i; continue; }
        if (w->owner_id == owner && !w->owner_closed) mine++;
    }
    if (slot < 0 || mine >= USERWIN_PER_PROC) return -1;

    uint32_t* buf = (uint32_t*)kzalloc((size_t)cw * ch * sizeof(uint32_t));
    if (!buf) return -1;
    for (uint32_t i = 0; i < cw * ch; i++) buf[i] = USERWIN_BG;

    // Posição: centralizada, com um deslocamento em cascata por slot
    // para janelas novas não ficarem exatamente uma sobre a outra.
    uint32_t ow = outer_w_for(cw);
    uint32_t oh = outer_h_for(ch);
    int32_t  x  = ((int32_t)sw - (int32_t)ow) / 2 + (slot % 5) * 28;
    int32_t  y  = ((int32_t)(sh - TASKBAR_H) - (int32_t)oh) / 2 + (slot % 5) * 28;
    if (x + (int32_t)ow > (int32_t)sw)              x = (int32_t)sw - (int32_t)ow;
    if (y + (int32_t)oh > (int32_t)(sh - TASKBAR_H)) y = (int32_t)(sh - TASKBAR_H) - (int32_t)oh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    UserWindow* w = new UserWindow(x, y, ow, oh,
                                   (title && title[0]) ? title : "App",
                                   owner, buf, cw, ch);
    if (!w) { kfree(buf); return -1; }

    // WM cheio (MAX_WINDOWS): o destrutor libera o canvas; handle ainda
    // é -1, então não mexe na tabela.
    if (!wm_register(w)) { delete w; return -1; }

    {
        IrqOff off;
        w->handle = slot;
        s_win[slot] = w;
    }
    return slot;
}

extern "C" int userwin_destroy(uint64_t owner, int64_t handle) {
    UserWindow* w = lookup(owner, handle);
    if (!w) return -1;
    // Regra 1: só marca. A GUI fecha em userwin_reap().
    w->owner_closed = true;
    return 0;
}

extern "C" int userwin_fill(uint64_t owner, int64_t handle, const haos_win_rect_t* r) {
    UserWindow* w = lookup(owner, handle);
    if (!w || !w->canvas) return -1;
    if (r->w <= 0 || r->h <= 0) return 0;

    int64_t x0 = r->x, y0 = r->y;
    int64_t x1 = x0 + r->w, y1 = y0 + r->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)w->canvas_w) x1 = w->canvas_w;
    if (y1 > (int64_t)w->canvas_h) y1 = w->canvas_h;
    if (x0 >= x1 || y0 >= y1) return 0;

    uint32_t color = r->color & 0xFFFFFF;
    for (int64_t y = y0; y < y1; y++) {
        uint32_t* row = w->canvas + (size_t)y * w->canvas_w;
        for (int64_t x = x0; x < x1; x++) row[x] = color;
    }
    return 0;
}

// Desenha uma string no canvas com a MESMA fonte 8x16 do resto da GUI.
// Cada byte é um índice da fonte (CP437) — sem decodificar UTF-8.
// '\n' volta ao x inicial e desce uma linha.
extern "C" int userwin_text(uint64_t owner, int64_t handle, int32_t x, int32_t y,
                            uint32_t fg, uint32_t bg, uint32_t flags,
                            const char* s, uint32_t len) {
    UserWindow* w = lookup(owner, handle);
    if (!w || !w->canvas) return -1;

    fg &= 0xFFFFFF;
    bg &= 0xFFFFFF;
    bool opaque = (flags & HAOS_TEXT_OPAQUE) != 0;
    int32_t cx = x;

    for (uint32_t i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)s[i];
        if (ch == '\n') { cx = x; y += FONT_H; continue; }

        // Caractere inteiramente fora do canvas: só avança.
        if (cx + FONT_W > 0 && cx < (int32_t)w->canvas_w &&
            y + FONT_H > 0  && y  < (int32_t)w->canvas_h) {
            const uint8_t* glyph = font_get(ch);
            for (int row = 0; row < FONT_H; row++) {
                int32_t py = y + row;
                if (py < 0 || py >= (int32_t)w->canvas_h) continue;
                uint32_t* line = w->canvas + (size_t)py * w->canvas_w;
                uint8_t bits = glyph[row];
                for (int col = 0; col < FONT_W; col++) {
                    int32_t px = cx + col;
                    if (px < 0 || px >= (int32_t)w->canvas_w) continue;
                    if (bits & (1 << (7 - col)))   line[px] = fg;
                    else if (opaque)               line[px] = bg;
                }
            }
        }
        cx += FONT_W;
    }
    return 0;
}

// `pixels` aponta para memória de USUÁRIO já validada por
// kernel/syscall.c (w*h*4 bytes legíveis) — esta é a única função da
// API que lê memória do processo.
extern "C" int userwin_blit(uint64_t owner, int64_t handle, int32_t x, int32_t y,
                            int32_t bw, int32_t bh, const uint32_t* pixels) {
    UserWindow* w = lookup(owner, handle);
    if (!w || !w->canvas || !pixels) return -1;
    if (bw <= 0 || bh <= 0 || bw > (int32_t)USERWIN_MAX_DIM || bh > (int32_t)USERWIN_MAX_DIM) return -1;

    for (int32_t r = 0; r < bh; r++) {
        int64_t dy = (int64_t)y + r;
        if (dy < 0 || dy >= (int64_t)w->canvas_h) continue;

        int64_t sx0 = 0;                  // primeira coluna da ORIGEM que aparece
        int64_t dx  = x;                  // coluna de destino correspondente
        if (dx < 0) { sx0 = -dx; dx = 0; }
        int64_t n = (int64_t)bw - sx0;
        if (dx + n > (int64_t)w->canvas_w) n = (int64_t)w->canvas_w - dx;
        if (n <= 0) continue;

        kmemcpy(w->canvas + (size_t)dy * w->canvas_w + dx,
                pixels + (size_t)r * bw + sx0,
                (size_t)n * sizeof(uint32_t));
    }
    return 0;
}

extern "C" int userwin_poll(uint64_t owner, int64_t handle, haos_win_event_t* out) {
    UserWindow* w = lookup(owner, handle);
    if (!w) return -1;
    return w->pop_event(out) ? 1 : 0;
}

extern "C" int userwin_info(uint64_t owner, int64_t handle, haos_win_info_t* out) {
    UserWindow* w = lookup(owner, handle);
    if (!w) return -1;
    out->w = w->canvas_w;
    out->h = w->canvas_h;
    return 0;
}

extern "C" int userwin_set_title(uint64_t owner, int64_t handle, const char* title, uint32_t len) {
    UserWindow* w = lookup(owner, handle);
    if (!w || !title) return -1;
    if (len > sizeof(w->title) - 1) len = sizeof(w->title) - 1;
    IrqOff off;                                   // a GUI pode estar desenhando o título agora
    uint32_t i = 0;
    for (; i < len && title[i]; i++) w->title[i] = title[i];
    w->title[i] = '\0';
    return 0;
}

extern "C" void userwin_reap(void) {
    // Tema mudou? Avisa todas as janelas de usuário (HAOS_EV_THEME).
    static uint32_t last_theme = 0;
    uint32_t tg = theme_generation();
    if (last_theme == 0) last_theme = tg;          // primeira passada: só memoriza
    else if (tg != last_theme) {
        last_theme = tg;
        for (int i = 0; i < (int)USERWIN_MAX; i++)
            if (s_win[i]) s_win[i]->push_event(HAOS_EV_THEME, 0, 0, 0);
    }

    for (int i = 0; i < (int)USERWIN_MAX; i++) {
        UserWindow* w = s_win[i];
        if (!w) continue;
        bool dead = w->owner_closed || !task_is_alive(w->owner_id);   // int 0/1
        // wm_close deleta a janela; o destrutor limpa s_win[i] e libera o canvas.
        if (dead) wm_close(w);
    }
}
