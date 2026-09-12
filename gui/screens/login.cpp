// gui/screens/login.cpp — Tela de login
//
// Substitui a antiga draw_welcome_screen() (puramente decorativa,
// sem estado nem interação real além de "aperte ENTER ou espere 5s").
// Esta tela:
//   - Lê /etc/user.cfg no VFS para saber se já existe um usuário
//     configurado (fluxo de "bem-vindo de volta").
//   - Primeiro boot: pede um nome e, em seguida, oferece criar uma
//     senha opcional (com botão "Não quero senha" — não é obrigatório
//     ter senha nesta v1).
//   - Se o usuário configurou senha, reinicializações seguintes pedem
//     a senha antes de entrar.
//   - Mostra o wallpaper atual no fundo (wallpaper_draw), em vez do
//     gradiente fixo da welcome screen antiga.
//   - Usa o sistema de Widget/Container real (TextField + Button)
//     em vez de desenho manual.
//
// Propositalmente simples: o "hash" de senha aqui é um FNV-1a de 32
// bits, só para não gravar a senha em texto puro à toa — NÃO é
// segurança de verdade (sem salt, sem verificação de força, sem
// proteção contra quem tem acesso físico ao disco). Adequado para
// uma v1 single-user sem isolamento de processo; deve ser revisto
// quando houver multiusuário real 

#include "login.h"
#include "../core/container.h"
#include "../core/layout.h"
#include "../core/theme.h"
#include "../widgets_basic.h"
#include "../widgets_input.h"
#include "../wallpaper.h"
#include "../../drivers/fb.h"
#include "../../kernel/keyboard.h"
#include "../../drivers/mouse.h"
#include "../../kernel/memory.h"
#include "../../kernel/lang.h"
#include "../../fs/vfs.h"

extern "C" volatile uint64_t timer_ticks;

#define CARD_W          420
#define CARD_H          260
#define NAME_MAX        63
#define LANG_SWITCH_W   148
#define LANG_SWITCH_H   30
#define LANG_SWITCH_Y   16
#define LOGIN_FOOTER_H  32

// ---- Hash simples (FNV-1a 32-bit) -------------------------------
// Só para não gravar a senha em claro no disco — ver aviso acima.
static uint32_t fnv1a_hash(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void hash_to_hex(uint32_t h, char* out /* buf[9] */) {
    static const char* digits = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        out[i] = digits[h & 0xF];
        h >>= 4;
    }
    out[8] = '\0';
}

// ---- Estado carregado de /etc/user.cfg --------------------------
struct UserConfig {
    bool has_user     = false;
    bool has_password = false;
    char name[NAME_MAX + 1] = {0};
    char password_hash_hex[9] = {0}; // 32 bits em hex = 8 chars + '\0'
};

// Formato de /etc/user.cfg, uma diretiva por linha:
//   name=<nome>
//   pwhash=<8 hex chars>       (linha omitida se não há senha)
static VfsNode* ensure_etc_dir() {
    VfsNode* root = vfs_root();
    VfsNode* etc  = vfs_find_child(root, "etc");
    if (!etc) etc = vfs_mkdir(root, "etc");
    return etc;
}

static bool line_starts_with(const char* data, uint32_t size, uint32_t pos,
                              const char* prefix) {
    uint32_t plen = (uint32_t)kstrlen(prefix);
    if (pos + plen > size) return false;
    for (uint32_t i = 0; i < plen; i++) {
        if (data[pos + i] != prefix[i]) return false;
    }
    return true;
}

static uint32_t read_line_value(const char* data, uint32_t size, uint32_t pos,
                                 char* out, size_t out_sz) {
    uint32_t j = 0;
    while (pos < size && data[pos] != '\n' && data[pos] != '\0' &&
           j < out_sz - 1) {
        out[j++] = data[pos++];
    }
    out[j] = '\0';
    // Avança até depois do '\n', se houver, para o chamador continuar
    // do início da próxima linha.
    while (pos < size && data[pos] != '\n') pos++;
    if (pos < size) pos++;
    return pos;
}

static UserConfig load_user_config() {
    UserConfig cfg;
    VfsNode* etc = ensure_etc_dir();
    if (!etc) return cfg;
    VfsNode* node = vfs_find_child(etc, "user.cfg");
    if (!node || !node->data || node->size == 0) return cfg;

    uint32_t pos = 0;
    while (pos < node->size) {
        if (line_starts_with(node->data, node->size, pos, "name=")) {
            pos = read_line_value(node->data, node->size, pos + 5,
                                   cfg.name, sizeof(cfg.name));
            cfg.has_user = cfg.name[0] != '\0';
        } else if (line_starts_with(node->data, node->size, pos, "pwhash=")) {
            char buf[16];
            pos = read_line_value(node->data, node->size, pos + 7,
                                   buf, sizeof(buf));
            kstrncpy(cfg.password_hash_hex, buf, sizeof(cfg.password_hash_hex) - 1);
            cfg.has_password = cfg.password_hash_hex[0] != '\0';
        } else {
            // Linha desconhecida/futura — pula até o próximo '\n'.
            while (pos < node->size && node->data[pos] != '\n') pos++;
            if (pos < node->size) pos++;
        }
    }
    return cfg;
}

// Grava nome + (opcionalmente) hash de senha em /etc/user.cfg.
// password pode ser nullptr/"" — nesse caso nenhuma linha pwhash= é
// escrita, e o próximo boot não pede senha.
static void save_user_config(const char* name, const char* password) {
    VfsNode* etc = ensure_etc_dir();
    if (!etc) return;
    VfsNode* node = vfs_find_child(etc, "user.cfg");
    if (!node) node = vfs_touch(etc, "user.cfg");
    if (!node) return;

    char buf[192];
    uint32_t i = 0;
    const char* p1 = "name=";
    while (p1[i] && i < sizeof(buf)) { buf[i] = p1[i]; i++; }
    uint32_t j = 0;
    while (name[j] && i < sizeof(buf) - 2) { buf[i++] = name[j++]; }
    buf[i++] = '\n';

    if (password && password[0] != '\0') {
        char hex[9];
        hash_to_hex(fnv1a_hash(password), hex);
        const char* p2 = "pwhash=";
        uint32_t k = 0;
        while (p2[k] && i < sizeof(buf) - 10) { buf[i++] = p2[k++]; }
        uint32_t m = 0;
        while (hex[m] && i < sizeof(buf) - 2) { buf[i++] = hex[m++]; }
        buf[i++] = '\n';
    }
    buf[i] = '\0';

    vfs_write(node, buf);
}

// ---- UI --------------------------------------------------------
// Três telas possíveis, controladas por m_stage:
//   NAME      — primeiro boot, passo 1: pedir nome
//   PASSWORD  — primeiro boot, passo 2: criar senha (ou pular)
//   ENTER_PW  — usuário já existe E tem senha: pedir a senha
//   WELCOME   — usuário já existe e não tem senha: só confirmar
enum class LoginStage { NAME, PASSWORD, ENTER_PW, WELCOME };

static void on_primary_clicked(Button* self);
static void on_skip_password_clicked(Button* self);

static bool language_switch_contains(uint32_t sw, int32_t mx, int32_t my) {
    int32_t x = (int32_t)sw - LANG_SWITCH_W - 18;
    return mx >= x && mx < x + LANG_SWITCH_W &&
           my >= LANG_SWITCH_Y && my < LANG_SWITCH_Y + LANG_SWITCH_H;
}

static void draw_language_switch(uint32_t sw, bool hovered, bool pressed) {
    int32_t x = (int32_t)sw - LANG_SWITCH_W - 18;
    uint32_t bg = pressed ? 0x315E9A : hovered ? 0x284D80 : 0x172B4D;
    fb_draw_rounded_rect(x, LANG_SWITCH_Y, LANG_SWITCH_W, LANG_SWITCH_H, bg, 6);
    fb_draw_rect((uint32_t)x, LANG_SWITCH_Y, LANG_SWITCH_W, LANG_SWITCH_H,
                 0x5D8FD5, 1);
    fb_draw_string((uint32_t)(x + 10), LANG_SWITCH_Y + 7,
                   tr(STR_CONFIG_LANGUAGE), 0xD5E7FF, 0, true);
    int32_t options_x = x + 76;
    bool pt_active = lang_get() == LANG_PT;
    fb_draw_string((uint32_t)options_x, LANG_SWITCH_Y + 7, "PT", 
                   pt_active ? 0xFFFFFF : 0x8FA8C8, 0, true);
    fb_draw_string((uint32_t)(options_x + 30), LANG_SWITCH_Y + 7, "EN",
                   pt_active ? 0x8FA8C8 : 0xFFFFFF, 0, true);
}

static void draw_login_footer(uint32_t sw, uint32_t sh) {
    uint32_t y = sh > LOGIN_FOOTER_H ? sh - LOGIN_FOOTER_H : 0;
    fb_fill_rect(0, y, sw, LOGIN_FOOTER_H, COLOR_TASKBAR_BG);
    fb_fill_rect(0, y, sw, 1, COLOR_TASKBAR_LINE);
}

class LoginCard : public Container {
public:
    LoginCard(int32_t x, int32_t y, uint32_t w, uint32_t h,
              LoginStage stage, const char* known_name)
        : Container(x, y, w, h), m_stage(stage)
    {
        kstrncpy(m_known_name, known_name, sizeof(m_known_name) - 1);
        build_for_stage(stage, w);
    }

    void draw(int32_t ox, int32_t oy) override {
        const Theme* t = theme_current();
        int32_t ax = ox + bounds.x, ay = oy + bounds.y;

        fb_draw_shadow(ax + 6, ay + 8, bounds.w, bounds.h);
        fb_draw_rounded_rect(ax - 2, ay - 2, bounds.w + 4, bounds.h + 4, 0x14264A, 12);
        fb_draw_rounded_rect(ax, ay, bounds.w, bounds.h, t->panel_bg, 10);

        fb_draw_string_centered(ax, ay + 16, bounds.w, 20,
                                 tr(STR_LOGIN_TITLE), t->label_fg, 0, true);

        Container::draw(ox, oy);
    }

    LoginStage stage() const { return m_stage; }
    const char* name_field_text() const { return m_name_field ? m_name_field->text() : m_known_name; }
    const char* password_field_text() const { return m_pw_field ? m_pw_field->text() : ""; }

    // Reconstrói a tela para o próximo estágio (chamado quando o
    // usuário avança do passo "nome" para o passo "senha", ou quando
    // uma senha errada precisa ser digitada de novo). Limpa todos os
    // widgets do estágio anterior antes de construir os novos —
    // sem isso, os widgets antigos continuariam na lista, recebendo
    // eventos e sendo desenhados por baixo/por cima dos novos.
    void advance_to(LoginStage next, uint32_t w) {
        m_stage = next;
        m_children.clear();
        m_focused_child = nullptr; // protected em Container, acessível aqui via herança
        m_name_field = nullptr;
        m_pw_field   = nullptr;
        m_primary_button = nullptr;
        m_skip_button    = nullptr;
        build_for_stage(next, w);
    }

    void refresh_language(uint32_t w) {
        char name_text[128] = {0};
        char password_text[128] = {0};
        if (m_name_field) kstrncpy(name_text, m_name_field->text(), sizeof(name_text) - 1);
        if (m_pw_field) kstrncpy(password_text, m_pw_field->text(), sizeof(password_text) - 1);

        m_children.clear();
        m_focused_child = nullptr;
        m_name_field = nullptr;
        m_pw_field = nullptr;
        m_primary_button = nullptr;
        m_skip_button = nullptr;
        build_for_stage(m_stage, w);

        if (m_name_field) m_name_field->set_text(name_text);
        if (m_pw_field) m_pw_field->set_text(password_text);
        focus_first();
    }

private:
    LoginStage m_stage;
    Button*    m_primary_button = nullptr;
    Button*    m_skip_button    = nullptr;
    TextField* m_name_field     = nullptr;
    TextField* m_pw_field       = nullptr;
    char       m_known_name[NAME_MAX + 1] = {0};

    void build_for_stage(LoginStage stage, uint32_t w) {
        VStack* stack = new VStack(24, 56, w - 48);

        switch (stage) {
            case LoginStage::NAME:
                stack->add(new Label(0, 0, tr(STR_LOGIN_FIRST_BOOT_TITLE)));
                stack->add(new Label(0, 0, tr(STR_LOGIN_NAME_PROMPT)));
                m_name_field = new TextField(0, 0, w - 48, 28, tr(STR_LOGIN_NAME_PLACEHOLDER));
                stack->add(m_name_field);
                m_primary_button = new Button(0, 0, w - 48, 34, tr(STR_LOGIN_NEXT_BTN));
                m_primary_button->set_on_click(on_primary_clicked);
                stack->add(m_primary_button);
                break;

            case LoginStage::PASSWORD:
                stack->add(new Label(0, 0, tr(STR_LOGIN_PASSWORD_TITLE)));
                stack->add(new Label(0, 0, tr(STR_LOGIN_PASSWORD_PROMPT)));
                m_pw_field = new TextField(0, 0, w - 48, 28, tr(STR_LOGIN_PASSWORD_PLACEHOLDER));
                m_pw_field->set_password_mode(true);
                stack->add(m_pw_field);
                m_primary_button = new Button(0, 0, w - 48, 34, tr(STR_LOGIN_ENTER_BTN));
                m_primary_button->set_on_click(on_primary_clicked);
                stack->add(m_primary_button);
                m_skip_button = new Button(0, 0, w - 48, 30, tr(STR_LOGIN_SKIP_PASSWORD_BTN));
                m_skip_button->set_on_click(on_skip_password_clicked);
                stack->add(m_skip_button);
                break;

            case LoginStage::ENTER_PW:
                stack->add(new Label(0, 0, tr(STR_LOGIN_WELCOME_BACK)));
                stack->add(new Label(0, 0, m_known_name));
                stack->add(new Label(0, 0, tr(STR_LOGIN_PASSWORD_PROMPT_ENTER)));
                m_pw_field = new TextField(0, 0, w - 48, 28, tr(STR_LOGIN_PASSWORD_PLACEHOLDER_ENTER));
                m_pw_field->set_password_mode(true);
                stack->add(m_pw_field);
                m_primary_button = new Button(0, 0, w - 48, 34, tr(STR_LOGIN_ENTER_BTN));
                m_primary_button->set_on_click(on_primary_clicked);
                stack->add(m_primary_button);
                break;

            case LoginStage::WELCOME:
                stack->add(new Label(0, 0, tr(STR_LOGIN_WELCOME_BACK)));
                stack->add(new Label(0, 0, m_known_name));
                m_primary_button = new Button(0, 0, w - 48, 34, tr(STR_LOGIN_ENTER_BTN));
                m_primary_button->set_on_click(on_primary_clicked);
                stack->add(m_primary_button);
                break;
        }

        add(stack);
        focus_first();
    }
};

static LoginCard*  s_card = nullptr;
static bool        s_entered = false;
static bool        s_advance_requested = false;
static bool        s_skip_password_requested = false;
static bool        s_language_hovered = false;
static bool        s_language_pressed = false;

static void on_primary_clicked(Button* /*self*/) {
    if (s_card && s_card->stage() == LoginStage::NAME) {
        s_advance_requested = true;
    } else {
        s_entered = true;
    }
}

static void on_skip_password_clicked(Button* /*self*/) {
    s_skip_password_requested = true;
    s_entered = true;
}

// Roda o loop de eventos até s_entered (ou até um botão pedir avanço
// de estágio, tratado pelo chamador). Devolve o motivo da saída via
// as flags globais acima, que o chamador reseta a cada estágio.
static void run_stage_loop(uint32_t sw, uint32_t sh) {
    bool was_pressed = false;
    while (!s_entered && !s_advance_requested) {
        mouse_process();
        mouse_snap();

        int32_t mx = mouse_get_x(), my = mouse_get_y();
        bool pressed = mouse_left_pressed();
        s_language_hovered = language_switch_contains(sw, mx, my);

        if (pressed && !was_pressed) {
            if (s_language_hovered) {
                s_language_pressed = true;
            } else {
                WidgetEvent ev{EventType::MouseDown,
                               mx - s_card->bounds.x,
                               my - s_card->bounds.y, 0, 0};
                s_card->on_event(ev);
            }
        } else if (!pressed && was_pressed) {
            if (s_language_pressed) {
                if (s_language_hovered) {
                    int32_t switch_x = (int32_t)sw - LANG_SWITCH_W - 18;
                    lang_set(mx < switch_x + 103 ? LANG_PT : LANG_EN);
                    s_card->refresh_language(CARD_W);
                }
                s_language_pressed = false;
            } else {
                WidgetEvent ev{EventType::MouseUp,
                               mx - s_card->bounds.x,
                               my - s_card->bounds.y, 0, 0};
                s_card->on_event(ev);
            }
        } else if (!s_language_pressed) {
            WidgetEvent ev{EventType::MouseMove,
                           mx - s_card->bounds.x,
                           my - s_card->bounds.y, 0, 0};
            s_card->on_event(ev);
        }
        was_pressed = pressed;

        uint8_t c = keyboard_getchar();
        if (c) {
            if (c == '\t') {
                s_card->focus_next();
            } else if (c == '\n' || c == '\r') {
                on_primary_clicked(nullptr);
            } else {
                WidgetEvent ev{EventType::KeyDown, 0, 0, c, 0};
                s_card->on_event(ev);
            }
        }

        wallpaper_draw(sw, sh); // fundo real do desktop, não mais um gradiente fixo
        // wallpaper_draw reserva a faixa inferior para a taskbar. Durante
        // o login não há taskbar, então ela precisa ser redesenhada a cada
        // frame para não deixar rastros do cursor.
        draw_login_footer(sw, sh);
        s_card->draw(0, 0);
        draw_language_switch(sw, s_language_hovered, s_language_pressed);
        fb_draw_cursor((uint32_t)mx, (uint32_t)my, CURSOR_NORMAL);
        fb_flip();

        __asm__("pause");
    }
}

extern "C" const char* login_get_current_username(void) {
    static char cached_name[NAME_MAX + 1] = {0};
    UserConfig cfg = load_user_config();
    if (cfg.has_user) {
        kstrncpy(cached_name, cfg.name, sizeof(cached_name) - 1);
    } else {
        cached_name[0] = '\0';
    }
    return cached_name;
}

void run_login_screen(void) {
    uint32_t sw = fb_width(), sh = fb_height();
    UserConfig cfg = load_user_config();

    int32_t cx = (int32_t)((sw - CARD_W) / 2);
    int32_t cy = (int32_t)((sh - CARD_H) / 2);

    char typed_name[NAME_MAX + 1] = {0};

    if (!cfg.has_user) {
        // ---- Primeiro boot: passo 1, nome ----
        s_entered = false; s_advance_requested = false; s_skip_password_requested = false;
        s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::NAME, "");
        run_stage_loop(sw, sh);

        const char* n = s_card->name_field_text();
        if (n[0] != '\0') {
            kstrncpy(typed_name, n, sizeof(typed_name) - 1);
        } else {
            kstrncpy(typed_name, tr(STR_LOGIN_DEFAULT_USER), sizeof(typed_name) - 1);
        }

        // ---- Primeiro boot: passo 2, senha opcional ----
        s_entered = false; s_advance_requested = false; s_skip_password_requested = false;
        s_card->advance_to(LoginStage::PASSWORD, CARD_W);
        run_stage_loop(sw, sh);

        if (s_skip_password_requested) {
            save_user_config(typed_name, nullptr);
        } else {
            save_user_config(typed_name, s_card->password_field_text());
        }

        delete s_card;
        s_card = nullptr;
        return;
    }

    // ---- Usuário já configurado ----
    if (cfg.has_password) {
        s_entered = false; s_advance_requested = false;
        s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::ENTER_PW, cfg.name);

        // Repete a tela até a senha bater — sem limite de tentativas
        // nesta v1 (não há bloqueio de conta/lockout ainda).
        while (true) {
            run_stage_loop(sw, sh);
            char hex[9];
            hash_to_hex(fnv1a_hash(s_card->password_field_text()), hex);
            bool ok = kstrlen(hex) == kstrlen(cfg.password_hash_hex);
            if (ok) {
                for (uint32_t i = 0; hex[i]; i++) {
                    if (hex[i] != cfg.password_hash_hex[i]) { ok = false; break; }
                }
            }
            if (ok) break;
            s_entered = false;
            s_card->advance_to(LoginStage::ENTER_PW, CARD_W);
        }
    } else {
        s_entered = false; s_advance_requested = false;
        s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::WELCOME, cfg.name);
        run_stage_loop(sw, sh);
    }

    delete s_card;
    s_card = nullptr;
}

