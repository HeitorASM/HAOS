// gui/screens/login.cpp — Tela de login
//
// Autentica contra o banco de usuários do kernel (kernel/user.h,
// /etc/passwd): senhas com salt + PBKDF2-HMAC-SHA256. Esta tela só cuida
// da interface; quem sabe criar contas, conferir senhas e iniciar a sessão
// é kernel/user.c.
//
// Fluxos:
//   - Nenhuma conta (primeiro boot): pede um nome e uma senha OPCIONAL e
//     cria a primeira conta, que é ADMINISTRADORA. Entra direto.
//   - Uma conta: mostra "bem-vindo de volta" (com senha, se houver).
//   - Várias contas: pergunta primeiro o nome de usuário, depois a senha.
//   - Senha errada: mensagem no lugar do texto do prompt + 1 s de espera
//     (somado ao custo do PBKDF2, freia tentativas por força bruta).
//   - Mostra o wallpaper atual no fundo e usa o sistema de Widget/Container.
//
// Contas antigas (/etc/user.cfg + hash FNV-1a) são migradas por
// user_init() e o hash fraco é trocado no primeiro login.

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
#include "../../kernel/user.h"

extern "C" volatile uint64_t timer_ticks;

#define CARD_W          420
#define CARD_H          260
#define NAME_MAX        63
#define LANG_SWITCH_W   148
#define LANG_SWITCH_H   30
#define LANG_SWITCH_Y   16
#define LOGIN_FOOTER_H  32

// ---- UI --------------------------------------------------------
enum class LoginStage { NAME, PASSWORD, ENTER_PW, WELCOME, USERNAME };

static void on_primary_clicked(Button* self);
static void on_skip_password_clicked(Button* self);

static bool language_switch_contains(uint32_t sw, int32_t mx, int32_t my) {
    int32_t x = (int32_t)sw - LANG_SWITCH_W - 18;
    return mx >= x && mx < x + LANG_SWITCH_W &&
           my >= LANG_SWITCH_Y && my < LANG_SWITCH_Y + LANG_SWITCH_H;
}

// Antes esta função tinha todas as cores hardcoded (azul escuro fixo)
// — em Retro e Algodão Doce o botão ficava com contraste ruim. Agora
// lê tudo do tema, então cada preset decide como o seletor se parece.
static void draw_language_switch(uint32_t sw, bool hovered, bool pressed) {
    const Theme* t = theme_current();

    int32_t x = (int32_t)sw - LANG_SWITCH_W - 18;
    uint32_t bg = pressed ? t->login_lang_bg_pressed
                : hovered ? t->login_lang_bg_hover
                          : t->login_lang_bg;
    fb_draw_rounded_rect(x, LANG_SWITCH_Y, LANG_SWITCH_W, LANG_SWITCH_H, bg, 6);
    fb_draw_rect((uint32_t)x, LANG_SWITCH_Y, LANG_SWITCH_W, LANG_SWITCH_H,
                 t->login_lang_border, 1);
    fb_draw_string((uint32_t)(x + 10), LANG_SWITCH_Y + 7,
                   tr(STR_CONFIG_LANGUAGE), t->login_lang_text, 0, true);

    int32_t options_x = x + 76;
    bool pt_active = lang_get() == LANG_PT;
    fb_draw_string((uint32_t)options_x, LANG_SWITCH_Y + 7, "PT",
                   pt_active ? t->login_lang_text_active
                             : t->login_lang_text_inactive, 0, true);
    fb_draw_string((uint32_t)(options_x + 30), LANG_SWITCH_Y + 7, "EN",
                   pt_active ? t->login_lang_text_inactive
                             : t->login_lang_text_active, 0, true);
}

// Faixa inferior que cobre a área reservada da taskbar (wallpaper_draw
// reserva essa faixa mas o desktop ainda não existe durante o login).
// Agora usa as cores do tema — sem isso, Retro ficava com uma faixa
// escura embaixo e o resto azul, e Candy com uma faixa cinza-azulada.
static void draw_login_footer(uint32_t sw, uint32_t sh) {
    const Theme* t = theme_current();
    uint32_t y = sh > LOGIN_FOOTER_H ? sh - LOGIN_FOOTER_H : 0;
    fb_fill_rect(0, y, sw, LOGIN_FOOTER_H, t->login_footer_bg);
    fb_fill_rect(0, y, sw, 1, t->login_footer_line);
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

    // Reconstrói o card na etapa `next`, opcionalmente com uma mensagem de
    // erro (que SUBSTITUI o texto do prompt — não adiciona um label novo,
    // para a altura do card não mudar).
    void show_stage(LoginStage next, uint32_t w, const char* error) {
        kstrncpy(m_error, error ? error : "", sizeof(m_error) - 1);
        m_error[sizeof(m_error) - 1] = '\0';
        advance_to(next, w);
    }

    void set_known_name(const char* name) {
        kstrncpy(m_known_name, name, sizeof(m_known_name) - 1);
        m_known_name[sizeof(m_known_name) - 1] = '\0';
    }

    void draw(int32_t ox, int32_t oy) override {
        const Theme* t = theme_current();
        int32_t ax = ox + bounds.x, ay = oy + bounds.y;

        if (t->window_shadow) {
            fb_draw_shadow(ax + 6, ay + 8, bounds.w, bounds.h);
        }
        // "Anel" externo (halo) agora usa a cor de borda do tema —
        // em Retro ele fica azul XP, em Candy rosa, etc.
        fb_draw_rounded_rect(ax - 2, ay - 2, bounds.w + 4, bounds.h + 4,
                             t->login_card_border, 12);
        fb_draw_rounded_rect(ax, ay, bounds.w, bounds.h, t->panel_bg, 10);

        fb_draw_string_centered(ax, ay + 16, bounds.w, 20,
                                 tr(STR_LOGIN_TITLE), t->label_fg, 0, true);

        Container::draw(ox, oy);
    }

    LoginStage stage() const { return m_stage; }
    const char* name_field_text() const { return m_name_field ? m_name_field->text() : m_known_name; }
    const char* password_field_text() const { return m_pw_field ? m_pw_field->text() : ""; }

    void advance_to(LoginStage next, uint32_t w) {
        m_stage = next;
        m_children.clear();
        m_focused_child = nullptr;
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
    char       m_error[96] = {0};

    void build_for_stage(LoginStage stage, uint32_t w) {
        VStack* stack = new VStack(24, 56, w - 48);

        switch (stage) {
            case LoginStage::NAME:
                stack->add(new Label(0, 0, tr(STR_LOGIN_FIRST_BOOT_TITLE)));
                stack->add(new Label(0, 0, m_error[0] ? m_error : tr(STR_LOGIN_NAME_PROMPT)));
                m_name_field = new TextField(0, 0, w - 48, 28, tr(STR_LOGIN_NAME_PLACEHOLDER));
                stack->add(m_name_field);
                m_primary_button = new Button(0, 0, w - 48, 34, tr(STR_LOGIN_NEXT_BTN));
                m_primary_button->set_on_click(on_primary_clicked);
                stack->add(m_primary_button);
                break;

            case LoginStage::USERNAME:
                stack->add(new Label(0, 0, tr(STR_LOGIN_USER_TITLE)));
                stack->add(new Label(0, 0, m_error[0] ? m_error : tr(STR_LOGIN_USER_PROMPT)));
                m_name_field = new TextField(0, 0, w - 48, 28, tr(STR_LOGIN_USER_PLACEHOLDER));
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
                stack->add(new Label(0, 0, m_error[0] ? m_error : tr(STR_LOGIN_PASSWORD_PROMPT_ENTER)));
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
    if (s_card && (s_card->stage() == LoginStage::NAME ||
                   s_card->stage() == LoginStage::USERNAME)) {
        s_advance_requested = true;
    } else {
        s_entered = true;
    }
}

static void on_skip_password_clicked(Button* /*self*/) {
    s_skip_password_requested = true;
    s_entered = true;
}

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

        wallpaper_draw(sw, sh);
        draw_login_footer(sw, sh);
        s_card->draw(0, 0);
        draw_language_switch(sw, s_language_hovered, s_language_pressed);
        fb_draw_cursor((uint32_t)mx, (uint32_t)my, CURSOR_NORMAL);
        fb_flip();

        __asm__("pause");
    }
}

extern "C" const char* login_get_current_username(void) {
    return session_username();
}

static char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Procura uma conta que PODE entrar (não é root/travada) ignorando
// maiúsculas/minúsculas — digitar "ANA" para a conta "ana" funciona.
// Retorna nullptr se não existir; *locked = true se existe mas não entra.
static const UserEntry* find_login_user(const char* name, bool* locked) {
    *locked = false;
    for (uint32_t i = 0; i < user_total(); i++) {
        const UserEntry* u = user_at(i);
        if (!u) continue;
        const char* a = u->name;
        const char* b = name;
        while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) { a++; b++; }
        if (*a || *b) continue;
        if (u->uid == UID_ROOT || (u->flags & USER_FLAG_LOCKED)) { *locked = true; return nullptr; }
        return u;
    }
    return nullptr;
}

static const UserEntry* first_regular_user() {
    for (uint32_t i = 0; i < user_total(); i++) {
        const UserEntry* u = user_at(i);
        if (u && u->uid != UID_ROOT && !(u->flags & USER_FLAG_LOCKED)) return u;
    }
    return nullptr;
}

static void reset_flags() {
    s_entered = false;
    s_advance_requested = false;
    s_skip_password_requested = false;
}

// 1 s de espera após uma senha errada (timer_ticks roda a 100 Hz).
static void wrong_password_penalty() {
    uint64_t until = timer_ticks + 100;
    while (timer_ticks < until) __asm__("pause");
}

// Primeiro boot: cria a primeira conta (administradora).
static const UserEntry* run_first_boot(uint32_t sw, uint32_t sh, int32_t cx, int32_t cy) {
    const char* error = "";
    while (true) {
        reset_flags();
        s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::NAME, "");
        s_card->show_stage(LoginStage::NAME, CARD_W, error);
        run_stage_loop(sw, sh);

        char typed_name[NAME_MAX + 1] = {0};
        kstrncpy(typed_name, s_card->name_field_text(), sizeof(typed_name) - 1);
        user_sanitize_name(typed_name);
        if (typed_name[0] == '\0') {
            kstrncpy(typed_name, tr(STR_LOGIN_DEFAULT_USER), sizeof(typed_name) - 1);
            user_sanitize_name(typed_name);
        }

        reset_flags();
        s_card->show_stage(LoginStage::PASSWORD, CARD_W, "");
        run_stage_loop(sw, sh);

        char password[PASSWORD_MAX + 1] = {0};
        if (!s_skip_password_requested)
            kstrncpy(password, s_card->password_field_text(), sizeof(password) - 1);

        delete s_card;
        s_card = nullptr;

        int r = user_create(typed_name, password, 1u);
        kmemset(password, 0, sizeof(password));
        if (r == USER_OK) {
            const UserEntry* u = user_find(typed_name);
            if (u) return u;
        }
        error = tr(STR_LOGIN_CREATE_FAILED);
    }
}

void run_login_screen(void) {
    uint32_t sw = fb_width(), sh = fb_height();

    user_init();   // carrega /etc/passwd (migra o formato antigo, se houver)

    int32_t cx = (int32_t)((sw - CARD_W) / 2);
    int32_t cy = (int32_t)((sh - CARD_H) / 2);

    const UserEntry* account = nullptr;

    if (user_regular_count() == 0) {
        account = run_first_boot(sw, sh, cx, cy);
    } else {
        // Conta única: vai direto. Várias: pergunta o nome primeiro.
        account = (user_regular_count() == 1) ? first_regular_user() : nullptr;
        const char* error = "";

        while (true) {
            if (!account) {
                reset_flags();
                s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::USERNAME, "");
                s_card->show_stage(LoginStage::USERNAME, CARD_W, error);
                run_stage_loop(sw, sh);

                bool locked = false;
                char typed[USER_NAME_MAX];
                kstrncpy(typed, s_card->name_field_text(), sizeof(typed) - 1);
                typed[sizeof(typed) - 1] = '\0';
                account = find_login_user(typed, &locked);
                delete s_card;
                s_card = nullptr;
                if (!account && locked) {
                    error = tr(STR_LOGIN_ACCOUNT_LOCKED);
                    continue;
                }
                if (!account) {
                    // Nome inexistente: finge que existe — pede a senha, gasta
                    // o mesmo tempo e dá a MESMA mensagem de senha errada, para
                    // não revelar quais contas existem.
                    reset_flags();
                    s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::ENTER_PW, typed);
                    s_card->show_stage(LoginStage::ENTER_PW, CARD_W, "");
                    run_stage_loop(sw, sh);
                    user_verify(typed, s_card->password_field_text(), nullptr);  // inexistente: só gasta o tempo
                    delete s_card;
                    s_card = nullptr;
                    wrong_password_penalty();
                    error = tr(STR_LOGIN_BAD_PASSWORD);
                    continue;
                }
                error = "";
            }

            if (user_has_password(account)) {
                reset_flags();
                s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::ENTER_PW, account->name);
                s_card->show_stage(LoginStage::ENTER_PW, CARD_W, error);
                run_stage_loop(sw, sh);

                const UserEntry* verified = nullptr;
                int r = user_verify(account->name, s_card->password_field_text(), &verified);
                delete s_card;
                s_card = nullptr;

                if (r == USER_OK && verified) { account = verified; break; }
                if (r == USER_ERR_THROTTLED) {
                    error = tr(STR_LOGIN_THROTTLED);
                } else {
                    wrong_password_penalty();
                    error = tr(STR_LOGIN_BAD_PASSWORD);
                }
                // Com várias contas volta a pedir o nome; com uma só, só a senha.
                if (user_regular_count() > 1) account = nullptr;
                continue;
            }

            // Sem senha: tela de boas-vindas e entra.
            reset_flags();
            s_card = new LoginCard(cx, cy, CARD_W, CARD_H, LoginStage::WELCOME, account->name);
            run_stage_loop(sw, sh);
            delete s_card;
            s_card = nullptr;

            const UserEntry* verified = nullptr;
            if (user_verify(account->name, "", &verified) == USER_OK && verified) {
                account = verified;
                break;
            }
            account = nullptr;   // inesperado (conta mudou?): recomeça
        }
    }

    session_start(account);
}
