#include "desktop.h"
#include "../wm.h"
#include "../elements/icons.h"
#include "../elements/taskbar.h"
#include "../elements/startmenu.h"
#include "../apps/terminal.h"
#include "../apps/about.h"
#include "../apps/config.h"
#include "../apps/editor.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/keyboard.h"
#include "../../drivers/mouse.h"
#include "../../kernel/types.h"
#include "../../kernel/memory.h"
#include "../../kernel/lang.h"
#include "../wallpaper.h"

extern "C" volatile uint64_t timer_ticks;

static bool start_menu_open = false;
static Window* terminal_win = nullptr;
static uint64_t last_render_tick = 0;

static void handle_desktop_key(uint8_t c);
static void desktop_handle_click(int32_t mx, int32_t my, uint32_t sw, uint32_t sh);

// -------------------------------------------------------------
// Tratamento de teclado (hotkeys)
// -------------------------------------------------------------
static void handle_desktop_key(uint8_t c) {
    if (c == 27) {
        start_menu_open = false;
        return;
    }

    if (start_menu_open) {
        if (c == 't' || c == 'T') {
            if (!terminal_win || !terminal_win->active) {
                uint32_t sw = fb_width(), sh = fb_height();
                terminal_win = terminal_create(
                    (int32_t)(sw/2 - 340), (int32_t)(sh/2 - 200));
            } else {
                wm_focus(terminal_win);
            }
            start_menu_open = false;
        } else if (c == 'a' || c == 'A') {
            open_about_window();
            start_menu_open = false;
        } else if (c == 'e' || c == 'E') {
            uint32_t sw = fb_width(), sh = fb_height();
            editor_create((int32_t)(sw/2 - 300), (int32_t)(sh/2 - 200), nullptr);
            start_menu_open = false;
        } else if (c == 'r' || c == 'R') {
            outb(0x64, 0xFE);
            while(1) __asm__("hlt");
        }
        return;
    }

    // Antes: checava focused->on_key (function pointer) antes de
    // despachar. Agora, todo Window tem on_event() (é virtual com
    // implementação padrão em Widget), então basta sempre despachar
    // quando existe uma janela focada — o próprio on_event decide
    // se trata ou ignora a tecla.
    Window* focused = wm_get_focused();
    if (focused) {
        wm_dispatch_key(c);
        return;
    }

    if (c == 's' || c == 'S') { start_menu_open = !start_menu_open; return; }
    if (c == 't' || c == 'T') {
        if (!terminal_win || !terminal_win->active) {
            uint32_t sw = fb_width(), sh = fb_height();
            terminal_win = terminal_create(
                (int32_t)(sw/2 - 340), (int32_t)(sh/2 - 200));
        } else {
            wm_focus(terminal_win);
        }
        return;
    }
    if (c == 'a' || c == 'A') { open_about_window(); return; }
    if (c == 'c' || c == 'C') { open_config_window(); return; }
    if (c == 'e' || c == 'E') {
        uint32_t sw = fb_width(), sh = fb_height();
        editor_create((int32_t)(sw/2 - 300), (int32_t)(sh/2 - 200), nullptr);
        return;
    }
}

// -------------------------------------------------------------
// Cliques nos ícones do desktop
// -------------------------------------------------------------
static void desktop_handle_click(int32_t mx, int32_t my, uint32_t sw, uint32_t sh) {
    if (mx >= ICON_TERM_X && mx < ICON_TERM_X + ICON_W &&
        my >= ICON_TERM_Y && my < ICON_TERM_Y + ICON_H + ICON_LABEL_H) {
        if (!terminal_win || !terminal_win->active) {
            terminal_win = terminal_create((int32_t)(sw/2 - 340), (int32_t)(sh/2 - 200));
        } else {
            wm_focus(terminal_win);
        }
        start_menu_open = false;
        return;
    }
    if (mx >= ICON_ABOUT_X && mx < ICON_ABOUT_X + ICON_W &&
        my >= ICON_ABOUT_Y && my < ICON_ABOUT_Y + ICON_H + ICON_LABEL_H) {
        open_about_window();
        start_menu_open = false;
        return;
    }
    if (mx >= ICON_CONF_X && mx < ICON_CONF_X + ICON_W &&
        my >= ICON_CONF_Y && my < ICON_CONF_Y + ICON_H + ICON_LABEL_H) {
        open_config_window();
        start_menu_open = false;
        return;
    }
    if (mx >= ICON_EDIT_X && mx < ICON_EDIT_X + ICON_W &&
        my >= ICON_EDIT_Y && my < ICON_EDIT_Y + ICON_H + ICON_LABEL_H) {
        editor_create((int32_t)(sw/2 - 300), (int32_t)(sh/2 - 200), nullptr);
        start_menu_open = false;
        return;
    }
}

// -------------------------------------------------------------
// Loop principal do desktop
// -------------------------------------------------------------
extern "C" void run_desktop(void) {
    uint32_t sw = fb_width(), sh = fb_height();
    mouse_set_bounds((int32_t)sw - 1, (int32_t)sh - 1);
    wallpaper_init();

    terminal_win = terminal_create((int32_t)(sw/2 - 340),
                                   (int32_t)(sh/2 - 200));

    bool was_pressed = false;

    while (1) {
        uint64_t ticks = timer_ticks;

        uint8_t c;
        while ((c = keyboard_getchar()) != 0)
            handle_desktop_key(c);

        mouse_process();
        mouse_snap();

        int32_t mx = mouse_get_x(), my = mouse_get_y();
        bool pressed = mouse_left_pressed();

        if (pressed && !was_pressed) {
            uint32_t ty = sh - TASKBAR_H;
            if (my >= (int32_t)ty) {
                if (mx >= 4 && mx < 4 + START_BTN_W)
                    start_menu_open = !start_menu_open;
                else if (terminal_win && terminal_win->active &&
                         mx >= START_BTN_W + 12 && mx < START_BTN_W + 12 + 130) {
                    terminal_win->minimized = false;
                    wm_focus(terminal_win);
                    start_menu_open = false;
                }
            }
            else if (start_menu_open) {
                uint32_t mh = 278;
                uint32_t menu_x = 4, menu_y = sh - TASKBAR_H - mh;
                if (mx >= (int32_t)menu_x && mx < (int32_t)(menu_x + 210)) {
                    int rel_y = my - (int32_t)menu_y - 50;
                    int item = rel_y / 34;
                    if (item == 0) {
                        if (!terminal_win || !terminal_win->active)
                            terminal_win = terminal_create(
                                (int32_t)(sw/2-340), (int32_t)(sh/2-200));
                        else wm_focus(terminal_win);
                        start_menu_open = false;
                    } else if (item == 1) {
                        open_about_window();
                        start_menu_open = false;
                    } else if (item == 2) {
                        editor_create((int32_t)(sw/2 - 300), (int32_t)(sh/2 - 200), nullptr);
                        start_menu_open = false;
                    } else if (item == 3) {
                        open_config_window();
                        start_menu_open = false;
                    } else if (item == 5) {
                        outb(0x64, 0xFE);
                        while(1) __asm__("hlt");
                    }
                } else {
                    start_menu_open = false;
                }
            }
            else {
                desktop_handle_click(mx, my, sw, sh);
            }

            wm_mouse_down(mx, my);
        }

        if (pressed && was_pressed)
            wm_mouse_move(mx, my);
        if (!pressed && was_pressed)
            wm_mouse_up();

        was_pressed = pressed;

        if (ticks - last_render_tick < 2) {
            __asm__("pause");
            continue;
        }
        last_render_tick = ticks;

        if (terminal_win)
            terminal_tick(terminal_win, ticks);

        editor_tick_all(ticks);

        wallpaper_draw(sw, sh);

        draw_desktop_icons();
        wm_draw_all();
        draw_taskbar(ticks, start_menu_open, terminal_win && terminal_win->active);

        if (start_menu_open)
            draw_start_menu();

        if (!start_menu_open) {
            fb_draw_string(START_BTN_W + 14, sh - TASKBAR_H + 13,
                           tr(STR_DESKTOP_HINT_BAR),
                           COLOR_TEXT_DIM, 0, true);
        }

        fb_draw_cursor((uint32_t)mx, (uint32_t)my);
        fb_flip();
    }
}
