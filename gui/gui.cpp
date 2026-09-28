#include "gui.h"
#include "core/wm.h"
#include "core/theme.h"       // theme_load_preferences()
#include "screens/boot.h"
#include "screens/login.h"
#include "screens/desktop.h"
#include "../kernel/keyboard.h"   
#include "../drivers/mouse.h"
#include "../kernel/types.h"
#include "wallpaper.h"

extern volatile uint64_t timer_ticks;

static GuiState state = GUI_STATE_BOOT;

extern "C" {

void gui_init(void) {
    // Carrega a preferência de tema salva em /etc/haos.theme ANTES
    // de qualquer janela nascer, para que tudo já apareça com o
    // visual escolhido pelo usuário desde o primeiro frame. Se o
    // arquivo não existir (primeiro boot), theme_load_preferences()
    // não mexe em nada e o padrão embutido é usado.
    theme_load_preferences();

    wm_init();
    wallpaper_init(); // antes do login, para que ele já mostre o wallpaper de fundo
    state = GUI_STATE_BOOT;
}

void gui_run(void) {
    run_boot_screen();

    state = GUI_STATE_WELCOME;
    run_login_screen(); // bloqueia até o usuário clicar Entrar / apertar ENTER

    state = GUI_STATE_DESKTOP;
    run_desktop();
}

} // extern "C"