#include "about.h"
#include "../wm.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"

class AboutWindow : public Window {
public:
    AboutWindow(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* title)
        : Window(x, y, w, h, title, WinType::About) {}

    void draw(int32_t ox, int32_t oy) override {
        // Desenha primeiro o chrome padrão (titlebar, borda, sombra,
        // fundo da área de conteúdo) — Window::draw() já faz isso.
        Window::draw(ox, oy);
        if (!active || minimized) return;

        Rect content = content_area_absolute();
        int32_t bx = content.x + 16;
        int32_t by = content.y + 16;

        fb_draw_string((uint32_t)bx, (uint32_t)by,
                       tr(STR_ABOUT_HEADER), COLOR_ACCENT, 0, true);
        by += 6;
        fb_fill_rect((uint32_t)bx, (uint32_t)by, 180, 1, 0x2A4A8A);
        by += 12;

        const char* lines[] = {
            tr(STR_ABOUT_VERSION),
            tr(STR_ABOUT_ARCH),
            tr(STR_ABOUT_BOOT),
            tr(STR_ABOUT_VIDEO),
            tr(STR_ABOUT_GUI),
            tr(STR_ABOUT_INPUT),
            tr(STR_ABOUT_KERNEL),
            "",
            tr(STR_ABOUT_FOOTER),
        };
        for (int i = 0; i < 9; i++) {
            uint32_t col = (lines[i][0] == 0) ? 0 : (i == 8 ? COLOR_TEXT_GRAY : COLOR_TEXT_LIGHT);
            if (lines[i][0])
                fb_draw_string((uint32_t)bx, (uint32_t)by, lines[i], col, 0, true);
            by += 20;
        }
    }
};

static AboutWindow* about_win = nullptr;

void open_about_window(void) {
    if (about_win && about_win->active) {
        about_win->minimized = false;
        wm_focus(about_win);
        return;
    }
    uint32_t sw = fb_width(), sh = fb_height();

    // AboutWindow não passa por wm_create() (que constrói um Window
    // genérico) — é criada diretamente, já que precisamos do tipo
    // concreto AboutWindow para o override de draw() funcionar.
    // wm_register() adiciona uma janela já construída externamente
    // à lista do WM (foco, z-order, fechar) sem instanciá-la de novo.
    about_win = new AboutWindow((int32_t)(sw / 2 - 220), (int32_t)(sh / 2 - 150),
                                440, 300, tr(STR_ABOUT_WINDOW_TITLE));
    wm_register(about_win);
}
