#pragma once
#include "types.h"

// ============================================================
//  lang.h — Sistema de internacionalização (i18n) do HAOS
//
//  Fornece um enum de IDs de string e uma função tr(id) que
//  retorna o texto no idioma atualmente selecionado.
//
//  Uso:
//      fb_draw_string(x, y, tr(STR_SETTINGS_TITLE), ...);
//
//  Para trocar de idioma em runtime:
//      lang_set(LANG_EN);
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LANG_PT = 0,   // Português (Brasil)
    LANG_EN = 1,   // English
    LANG_COUNT
} Lang;

typedef enum {
    // ---- Geral / comum ----
    STR_YES = 0,
    STR_NO,
    STR_OK,
    STR_CANCEL,
    STR_CLOSE,

    // ---- Boot screen ----
    STR_BOOT_SUBTITLE,
    STR_BOOT_MSG_00, STR_BOOT_MSG_01, STR_BOOT_MSG_02, STR_BOOT_MSG_03,
    STR_BOOT_MSG_04, STR_BOOT_MSG_05, STR_BOOT_MSG_06, STR_BOOT_MSG_07,
    STR_BOOT_MSG_08, STR_BOOT_MSG_09, STR_BOOT_MSG_10, STR_BOOT_MSG_11,
    STR_BOOT_MSG_12, STR_BOOT_MSG_13, STR_BOOT_MSG_14, STR_BOOT_MSG_15,
    STR_BOOT_MSG_16, STR_BOOT_MSG_17, STR_BOOT_MSG_18, STR_BOOT_MSG_19,

    // ---- Welcome screen ----
    STR_WELCOME_TITLE,
    STR_WELCOME_SUBTITLE,
    STR_WELCOME_SPECS,
    STR_WELCOME_FEATURE_1,
    STR_WELCOME_FEATURE_2,
    STR_WELCOME_ENTER_BTN,
    STR_WELCOME_HINT,

    // ---- Desktop / taskbar / start menu ----
    STR_START_BUTTON,
    STR_TERMINAL,
    STR_ABOUT,
    STR_SETTINGS,
    STR_RESTART,
    STR_SHUTDOWN,
    STR_MENU_CLOSE_HINT,
    STR_DESKTOP_HINT_BAR,
    STR_STARTMENU_HEADER,

    // ---- Ícones do desktop ----
    STR_ICON_TERMINAL,
    STR_ICON_ABOUT,
    STR_ICON_SETTINGS,

    // ---- Janela Sobre ----
    STR_ABOUT_WINDOW_TITLE,
    STR_ABOUT_HEADER,
    STR_ABOUT_VERSION,
    STR_ABOUT_ARCH,
    STR_ABOUT_BOOT,
    STR_ABOUT_VIDEO,
    STR_ABOUT_GUI,
    STR_ABOUT_INPUT,
    STR_ABOUT_KERNEL,
    STR_ABOUT_FOOTER,

    // ---- Janela Configurações ----
    STR_CONFIG_WINDOW_TITLE,
    STR_CONFIG_WALLPAPER,
    STR_CONFIG_DEFAULT_WALLPAPER,
    STR_CONFIG_MODE,
    STR_CONFIG_MODE_FILL,
    STR_CONFIG_MODE_CENTER,
    STR_CONFIG_MODE_TILE,
    STR_CONFIG_DEVICE,
    STR_CONFIG_CPU,
    STR_CONFIG_CORES,
    STR_CONFIG_RAM_TOTAL,
    STR_CONFIG_RAM_FREE,
    STR_CONFIG_HEAP,
    STR_CONFIG_LANGUAGE,
    STR_CONFIG_LANG_PT,
    STR_CONFIG_LANG_EN,
    STR_CONFIG_FOOTER_HINT,

    // ---- Terminal: mensagens estáticas ----
    STR_TERM_WELCOME_1,
    STR_TERM_WELCOME_2,
    STR_TERM_HELP_HEADER,
    STR_TERM_HELP_HELP,
    STR_TERM_HELP_CLEAR,
    STR_TERM_HELP_ECHO,
    STR_TERM_HELP_ABOUT,
    STR_TERM_HELP_DATE,
    STR_TERM_HELP_MEM,
    STR_TERM_HELP_REBOOT,
    STR_TERM_HELP_LANG,
    STR_TERM_HELP_FS_HEADER,
    STR_TERM_HELP_PWD,
    STR_TERM_HELP_LS,
    STR_TERM_HELP_CD,
    STR_TERM_HELP_MKDIR,
    STR_TERM_HELP_TOUCH,
    STR_TERM_HELP_CAT,
    STR_TERM_HELP_WRITE,
    STR_TERM_HELP_APPEND,
    STR_TERM_HELP_RM,
    STR_TERM_HELP_STAT,

    STR_TERM_ABOUT_TITLE,
    STR_TERM_ABOUT_ARCH,
    STR_TERM_ABOUT_BOOT,
    STR_TERM_ABOUT_VIDEO,
    STR_TERM_ABOUT_GUI,
    STR_TERM_ABOUT_INPUT,
    STR_TERM_ABOUT_FS,

    STR_TERM_MEM_HEAP,
    STR_TERM_MEM_SHADOW,
    STR_TERM_MEM_BGCACHE,
    STR_TERM_MEM_VFS,

    STR_TERM_LS_DIR_NOT_FOUND,
    STR_TERM_LS_NOT_A_DIR,
    STR_TERM_LS_EMPTY,
    STR_TERM_CD_NOT_FOUND,
    STR_TERM_CD_NOT_A_DIR,
    STR_TERM_MKDIR_NEED_NAME,
    STR_TERM_MKDIR_FAILED,
    STR_TERM_TOUCH_NEED_NAME,
    STR_TERM_TOUCH_FAILED,
    STR_TERM_CAT_NEED_NAME,
    STR_TERM_CAT_NOT_FOUND,
    STR_TERM_CAT_IS_DIR,
    STR_TERM_CAT_EMPTY,
    STR_TERM_WRITE_USAGE,
    STR_TERM_WRITE_EMPTY_NAME,
    STR_TERM_WRITE_NOT_A_FILE,
    STR_TERM_WRITE_DONE,
    STR_TERM_APPEND_USAGE,
    STR_TERM_APPEND_NOT_A_FILE,
    STR_TERM_APPEND_DONE,
    STR_TERM_RM_NEED_NAME,
    STR_TERM_RM_NOT_FOUND,
    STR_TERM_RM_CANT_ROOT,
    STR_TERM_RM_CANT_CWD,
    STR_TERM_RM_DONE,
    STR_TERM_STAT_NEED_NAME,
    STR_TERM_STAT_NOT_FOUND,
    STR_TERM_STAT_NAME,
    STR_TERM_STAT_TYPE,
    STR_TERM_STAT_TYPE_DIR,
    STR_TERM_STAT_TYPE_FILE,
    STR_TERM_STAT_SIZE,
    STR_TERM_STAT_ITEMS,
    STR_TERM_STAT_PATH,
    STR_TERM_CMD_NOT_FOUND,
    STR_TERM_CMD_NOT_FOUND_HINT,
    STR_TERM_DATE_LABEL,
    STR_TERM_TIME_LABEL,
    STR_TERM_LANG_SET_PT,
    STR_TERM_LANG_SET_EN,
    STR_TERM_LANG_USAGE,

    STR_COUNT
} StrID;

// Idioma atualmente ativo (padrão: LANG_PT)
void        lang_init(void);
void        lang_set(Lang l);
Lang        lang_get(void);
void        lang_toggle(void);          // alterna PT <-> EN
const char* tr(StrID id);               // retorna string traduzida
const char* lang_name(Lang l);          // nome do idioma p/ exibição

#ifdef __cplusplus
}
#endif
