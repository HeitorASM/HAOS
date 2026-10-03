// gui/screens/desktop.cpp — Loop principal do desktop
#include "desktop.h"
#include "../core/wm.h"
#include "../elements/icons.h"
#include "../elements/taskbar.h"
#include "../elements/startmenu.h"
#include "../elements/contextmenu.h"
#include "../core/userwin.h"
#include "../apps/terminal.h"
#include "../apps/about.h"
#include "../apps/config.h"
#include "../apps/editor.h"
#include "../apps/file_manager.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/keyboard.h"
#include "../../drivers/mouse.h"
#include "../../kernel/types.h"
#include "../../kernel/memory.h"
#include "../../kernel/lang.h"
#include "../../kernel/task.h"
#include "../wallpaper.h"

extern "C" volatile uint64_t timer_ticks;

static bool start_menu_open = false;
static Window* terminal_win = nullptr;
static uint64_t last_render_tick = 0;

static void handle_desktop_key(uint8_t c);
static void desktop_handle_click(int32_t mx, int32_t my, uint32_t sw, uint32_t sh);

static void handle_desktop_key(uint8_t c) {
    if (c == 27) {
        start_menu_open = false;
        contextmenu_close();
        return;
    }

    // Atalhos globais de janela (ver kernel/keyboard.c): Alt+Tab ou
    // Ctrl+Tab alterna para a próxima janela; Alt+F4 fecha a focada.
    // Tratados AQUI, antes de repassar a tecla à janela em foco, para
    // funcionarem independentemente do app que estiver ativo.
    if (c == KEY_SWITCH_WINDOW) {
        start_menu_open = false;
        contextmenu_close();
        wm_focus_next();
        return;
    }
    if (c == KEY_CLOSE_WINDOW) {
        Window* f = wm_get_focused();
        if (f) wm_close(f);
        return;
    }

    Window* focused = wm_get_focused();
    if (focused) {
        wm_dispatch_key(c);
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
    if (mx >= ICON_FILES_X && mx < ICON_FILES_X + ICON_W &&
        my >= ICON_FILES_Y && my < ICON_FILES_Y + ICON_H + ICON_LABEL_H) {
        file_manager_create((int32_t)(sw / 2 - 310), (int32_t)(sh / 2 - 220));
        start_menu_open = false;
        return;
    }
}

// -------------------------------------------------------------
// Ações do menu de contexto do desktop (botão direito)
// -------------------------------------------------------------
// `item` segue a ordem de contextmenu.h: 0 Terminal, 1 Editor,
// 2 Arquivos, 3 Configurações, 4 Sobre. Mesmas chamadas dos ícones
// do desktop / menu Iniciar.
static void desktop_context_action(int item, uint32_t sw, uint32_t sh) {
    switch (item) {
        case 0:
            if (!terminal_win || !terminal_win->active)
                terminal_win = terminal_create((int32_t)(sw/2 - 340), (int32_t)(sh/2 - 200));
            else
                wm_focus(terminal_win);
            break;
        case 1:
            editor_create((int32_t)(sw/2 - 300), (int32_t)(sh/2 - 200), nullptr);
            break;
        case 2:
            file_manager_create((int32_t)(sw/2 - 310), (int32_t)(sh/2 - 220));
            break;
        case 3:
            open_config_window();
            break;
        case 4:
            open_about_window();
            break;
        default:
            break;
    }
}

// -------------------------------------------------------------
// Loop principal do desktop
// -------------------------------------------------------------
extern "C" void run_desktop(void) {
    uint32_t sw = fb_width(), sh = fb_height();
    mouse_set_bounds((int32_t)sw - 1, (int32_t)sh - 1);
    // wallpaper_init() já rodou em gui_init(), antes do login — não
    // repetir aqui evitaria resetar uma escolha de wallpaper feita
    // durante a sessão anterior de configuração, quando isso passar
    // a persistir em disco.

    terminal_win = terminal_create((int32_t)(sw/2 - 340),
                                   (int32_t)(sh/2 - 200));

    bool was_pressed = false;

    // Throttle do reaper de tasks zumbis: rodar isso a cada frame do
    // desktop seria desperdício (na imensa maioria dos frames não há
    // nenhuma task zumbi para coletar). Uma vez por segundo é mais
    // que suficiente para a Fase 1 (poucas kernel threads de vida
    // curta) — reavaliar se/quando processos de usuário passarem a
    // nascer e morrer com muito mais frequência.
    uint64_t next_reap_tick = timer_ticks + 100; // pit_init(100) => 100 ticks/s

    while (1) {
        uint64_t ticks = timer_ticks;

        if (ticks >= next_reap_tick) {
            task_reap_zombies();
            next_reap_tick = ticks + 100;
        }

        uint8_t c;
        while ((c = keyboard_getchar()) != 0)
            handle_desktop_key(c);

        mouse_process();
        mouse_snap();

        int32_t mx = mouse_get_x(), my = mouse_get_y();
        bool pressed = mouse_left_pressed();
        int32_t scroll = mouse_get_scroll();

        if (scroll != 0) wm_mouse_scroll(mx, my, scroll);

        // Botão direito: abre o menu de contexto do desktop, mas só sobre
        // a área livre (nem taskbar, nem uma janela). Com o menu Iniciar
        // aberto, o clique direito apenas o fecha.
        if (mouse_right_clicked()) {
            contextmenu_close();
            if (start_menu_open) {
                start_menu_open = false;
            } else if (my < (int32_t)(sh - TASKBAR_H) && !wm_window_at(mx, my)) {
                contextmenu_open(mx, my);
            }
        }

        // Clique esquerdo: cada clique tem UM destino, seguindo a ordem
        // das camadas na tela (de cima para baixo): menu de contexto ->
        // taskbar -> menu Iniciar -> janelas -> ícones do desktop.
        // ANTES, o clique ia SEMPRE para os ícones do desktop (abrindo o
        // app do ícone mesmo quando caía dentro de uma janela por cima
        // dele) e SEMPRE para o WM (atravessando o menu Iniciar/taskbar
        // e podendo roubar o foco da janela que o item acabou de abrir).
        if (pressed && !was_pressed) {
            uint32_t ty = sh - TASKBAR_H;
            bool to_wm    = true;   // oferece o clique às janelas
            bool to_icons = false;  // se nenhuma janela o consumir, vale para os ícones

            bool in_ctx = contextmenu_is_open() && contextmenu_contains(mx, my);
            if (in_ctx) {
                int item = contextmenu_hit_test(mx, my);
                contextmenu_close();
                if (item >= 0) desktop_context_action(item, sw, sh);
                to_wm = false;
            } else {
                contextmenu_close(); // clique fora do menu o fecha e segue adiante
            }

            if (in_ctx) {
                // já tratado acima
            }
            else if (my >= (int32_t)ty) {
                if (mx >= 4 && mx < 4 + START_BTN_W) {
                    start_menu_open = !start_menu_open;
                } else {
                    Window* clicked = taskbar_hit_test(mx, my);
                    if (clicked) {
                        if (clicked->minimized) wm_unminimize(clicked); // volta como estava (maximizada continua maximizada)
                        else if (clicked->focused) wm_minimize(clicked); // clicar de novo na já focada minimiza (padrão Windows)
                        else wm_focus(clicked);
                        start_menu_open = false;
                    }
                }
                to_wm = false; // clique na taskbar não vai para as janelas
            }
            else if (start_menu_open) {
                int item = start_menu_hit_test(mx, my);
                // Item clicado: o menu consome o clique. Fora dos itens
                // (-1): só fecha o menu e deixa o clique seguir adiante.
                to_wm = (item == -1);
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
                } else if (item == 4) {
                    file_manager_create((int32_t)(sw / 2 - 310),
                                        (int32_t)(sh / 2 - 220));
                    start_menu_open = false;
                } else if (item == 6) {
                    outb(0x64, 0xFE);
                    while(1) __asm__("hlt");
                } else if (item == -1) {
                    start_menu_open = false;
                }
            }
            else {
                to_icons = true;
            }

            bool wm_consumed = to_wm && wm_mouse_down(mx, my);
            if (to_icons && !wm_consumed)
                desktop_handle_click(mx, my, sw, sh);
        }

        if (pressed && was_pressed)
            wm_mouse_move(mx, my);
        if (!pressed && was_pressed)
            wm_mouse_up(mx, my);

        was_pressed = pressed;

        // Ponto seguro para executar ações diferidas: todo o
        // processamento de entrada deste frame já terminou (nenhum
        // handler de evento está na pilha de chamadas), e o desenho
        // ainda não começou. É aqui que janelas pedidas de dentro de
        // handlers (ex.: o explorador abrindo um terminal ao
        // duplo-clicar num .hexec) são de fato criadas, sem modificar
        // as listas do WM no meio de uma iteração sobre elas.
        //
        // Fica ANTES do `continue` do throttle de render logo abaixo
        // de propósito: se ficasse depois, uma ação enfileirada
        // poderia esperar vários frames (ou nunca rodar, se o
        // throttle sempre pulasse essa parte) — o usuário veria o
        // duplo-clique simplesmente não fazer nada.
        wm_run_deferred_actions();

        // Fecha janelas de processos de usuário que terminaram (ou que
        // pediram SYS_WIN_DESTROY). Tem que ser AQUI, na task da GUI:
        // a destruição de uma janela nunca ocorre dentro da syscall.
        userwin_reap();

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
        draw_taskbar(ticks, start_menu_open);

        if (start_menu_open)
            draw_start_menu();

        if (contextmenu_is_open())
            draw_contextmenu(mx, my);

        // Cursor muda de ícone ao passar sobre uma borda/canto
        // redimensionável da janela focada (comportamento padrão
        // em Windows/GNOME/macOS) — resto do tempo é a seta normal.
        CursorType cursor = wm_get_cursor_hint(mx, my);
        fb_draw_cursor((uint32_t)mx, (uint32_t)my, cursor);
        fb_flip();
    }
}
