#include "config.h"
#include "../core/wm.h"
#include "../core/theme.h"
#include "../wallpaper.h"
#include "../sidebar_nav.h"
#include "../core/container.h"
#include "../core/layout.h"
#include "../widgets_basic.h"
#include "../../drivers/fb.h"
#include "../../kernel/types.h"
#include "../../kernel/memory.h"
#include "../../kernel/sysinfo.h"
#include "../../kernel/lang.h"
#include "../screens/login.h"

#define CFG_W   560
#define CFG_H   580
#define SIDEBAR_W 160

enum ConfigCategory { CAT_SYSTEM = 0, CAT_PERSONALIZATION = 1, CAT_ABOUT = 2 };

// ---- Painel: Sistema (conta + idioma) ----
class SystemPanel : public Panel {
public:
    SystemPanel(int32_t x, int32_t y, uint32_t w, uint32_t h) : Panel(x, y, w, h, false) {
        VStack* stack = new VStack(16, 16, w - 32);

        Label* title = new Label(0, 0, tr(STR_CONFIG_CATEGORY_SYSTEM));
        stack->add(title);

        stack->add(new Label(0, 0, tr(STR_CONFIG_ACCOUNT_SECTION)));
        HStack* account_row = new HStack(0, 0, 48, 12);
        Canvas* avatar = new Canvas(0, 0, 48, 48);
        avatar->set_on_draw(&SystemPanel::draw_avatar);
        account_row->add(avatar);
        m_username_label = new Label(0, 0, login_get_current_username());
        account_row->add(m_username_label);
        stack->add(account_row);

        Label* lang_label = new Label(0, 0, tr(STR_CONFIG_LANGUAGE));
        stack->add(lang_label);

        Label* lang_desc = new Label(0, 0, tr(STR_CONFIG_SECTION_LANGUAGE_DESC));
        stack->add(lang_desc);

        HStack* lang_row = new HStack(0, 0, 26);
        m_btn_pt = new Button(0, 0, 108, 26, tr(STR_CONFIG_LANG_PT));
        m_btn_en = new Button(0, 0, 108, 26, tr(STR_CONFIG_LANG_EN));
        m_btn_pt->set_on_click([](Button*) { lang_set(LANG_PT); });
        m_btn_en->set_on_click([](Button*) { lang_set(LANG_EN); });
        lang_row->add(m_btn_pt);
        lang_row->add(m_btn_en);
        stack->add(lang_row);

        add(stack);
    }

    void draw(int32_t ox, int32_t oy) override {
        Lang cur = lang_get();
        m_btn_pt->set_active_style(cur == LANG_PT);
        m_btn_en->set_active_style(cur == LANG_EN);
        m_username_label->set_text(login_get_current_username());
        Panel::draw(ox, oy);
    }

private:
    Button* m_btn_pt;
    Button* m_btn_en;
    Label*  m_username_label;

    static void draw_avatar(Canvas* /*self*/, int32_t ax, int32_t ay,
                             uint32_t w, uint32_t h) {
        const Theme* t = theme_current();
        uint32_t cx = (uint32_t)ax + w / 2;
        uint32_t cy = (uint32_t)ay + h / 2;
        uint32_t r  = (w < h ? w : h) / 2;

        fb_fill_circle(cx, cy, r, t->button_bg_pressed);

        const char* name = login_get_current_username();
        char initial[2] = { name[0] ? (char)(name[0] & ~0x20) : '?', '\0' };

        uint32_t text_w = fb_text_width(initial);
        fb_draw_string(cx - text_w / 2, cy - 8, initial, t->button_fg, 0, true);
    }
};

// ---- Painel: Personalização ----
class PersonalizationPanel : public Panel {
public:
    PersonalizationPanel(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : Panel(x, y, w, h, false)
    {
        // Espaçamento 4 (era 6). O painel não tem scroll, e esta é a
        // seção mais densa da tela de Configurações — precisa caber
        // tudo (wallpaper + modo + tema + formas + janelas + taskbar)
        // dentro da altura fixa do painel.
        VStack* stack = new VStack(16, 12, w - 32, 4);

        stack->add(new Label(0, 0, tr(STR_CONFIG_CATEGORY_PERSONALIZATION)));

        // ---------- Papel de parede ----------
        stack->add(new Label(0, 0, tr(STR_CONFIG_WALLPAPER)));

        int count = wallpaper_count();
        m_btn_default = new Button(0, 0, w - 32, 24, tr(STR_CONFIG_DEFAULT_WALLPAPER));
        m_btn_default->set_on_click([](Button*) { wallpaper_set(-1); });
        stack->add(m_btn_default);

        for (int i = 0; i < count && i < MAX_WALLPAPER_BTNS; i++) {
            m_btn_wallpaper[i] = new Button(0, 0, w - 32, 24, wallpaper_name(i));
            m_wallpaper_index[i] = i;
            m_btn_wallpaper[i]->set_tag(i);
            m_btn_wallpaper[i]->set_on_click([](Button* self) {
                wallpaper_set(self->tag());
            });
            stack->add(m_btn_wallpaper[i]);
        }
        m_wallpaper_count = count;

        stack->add(new Label(0, 0, tr(STR_CONFIG_MODE)));
        HStack* mode_row = new HStack(0, 0, 24, 6);
        m_btn_fill   = new Button(0, 0, 108, 24, tr(STR_CONFIG_MODE_FILL));
        m_btn_center = new Button(0, 0, 108, 24, tr(STR_CONFIG_MODE_CENTER));
        m_btn_tile   = new Button(0, 0, 108, 24, tr(STR_CONFIG_MODE_TILE));
        m_btn_fill->set_on_click([](Button*)   { wallpaper_set_mode(WALLPAPER_MODE_FILL); });
        m_btn_center->set_on_click([](Button*) { wallpaper_set_mode(WALLPAPER_MODE_CENTER); });
        m_btn_tile->set_on_click([](Button*)   { wallpaper_set_mode(WALLPAPER_MODE_TILE); });
        mode_row->add(m_btn_fill);
        mode_row->add(m_btn_center);
        mode_row->add(m_btn_tile);
        stack->add(mode_row);

        // ---------- Tema ----------
        stack->add(new Label(0, 0, tr(STR_CONFIG_THEME)));

        HStack* theme_row1 = new HStack(0, 0, 24, 6);
        m_btn_theme[THEME_PRESET_DEFAULT]  = new Button(0, 0,  96, 24, tr(STR_CONFIG_THEME_DEFAULT));
        m_btn_theme[THEME_PRESET_TERMINAL] = new Button(0, 0,  96, 24, tr(STR_CONFIG_THEME_TERMINAL));
        m_btn_theme[THEME_PRESET_XP]       = new Button(0, 0,  96, 24, tr(STR_CONFIG_THEME_XP));
        theme_row1->add(m_btn_theme[THEME_PRESET_DEFAULT]);
        theme_row1->add(m_btn_theme[THEME_PRESET_TERMINAL]);
        theme_row1->add(m_btn_theme[THEME_PRESET_XP]);
        stack->add(theme_row1);

        HStack* theme_row2 = new HStack(0, 0, 24, 6);
        m_btn_theme[THEME_PRESET_BW]    = new Button(0, 0, 148, 24, tr(STR_CONFIG_THEME_BW));
        m_btn_theme[THEME_PRESET_CANDY] = new Button(0, 0, 148, 24, tr(STR_CONFIG_THEME_CANDY));
        theme_row2->add(m_btn_theme[THEME_PRESET_BW]);
        theme_row2->add(m_btn_theme[THEME_PRESET_CANDY]);
        stack->add(theme_row2);

        for (int i = 0; i < THEME_PRESET_COUNT; i++) {
            m_btn_theme[i]->set_tag(i);
            m_btn_theme[i]->set_on_click([](Button* self) {
                theme_apply_preset(self->tag());
            });
        }

        // ---------- Formas (cantos de janela + formato dos botões) ----------
        stack->add(new Label(0, 0, tr(STR_CONFIG_SHAPES)));

        HStack* corners_row = new HStack(0, 0, 24, 6);
        m_btn_corners_round  = new Button(0, 0, 148, 24, tr(STR_CONFIG_CORNERS_ROUND));
        m_btn_corners_square = new Button(0, 0, 148, 24, tr(STR_CONFIG_CORNERS_SQUARE));
        m_btn_corners_round->set_on_click([](Button*)  { theme_set_window_rounded(true);  });
        m_btn_corners_square->set_on_click([](Button*) { theme_set_window_rounded(false); });
        corners_row->add(m_btn_corners_round);
        corners_row->add(m_btn_corners_square);
        stack->add(corners_row);

        HStack* buttons_row = new HStack(0, 0, 24, 6);
        m_btn_buttons_round  = new Button(0, 0, 148, 24, tr(STR_CONFIG_BUTTONS_ROUND));
        m_btn_buttons_square = new Button(0, 0, 148, 24, tr(STR_CONFIG_BUTTONS_SQUARE));
        m_btn_buttons_round->set_on_click([](Button*)  { theme_set_buttons_rounded(true);  });
        m_btn_buttons_square->set_on_click([](Button*) { theme_set_buttons_rounded(false); });
        buttons_row->add(m_btn_buttons_round);
        buttons_row->add(m_btn_buttons_square);
        stack->add(buttons_row);

        // ---------- Janelas (sombra) ----------
        stack->add(new Label(0, 0, tr(STR_CONFIG_WINDOW_STYLE)));

        HStack* shadow_row = new HStack(0, 0, 24, 6);
        m_btn_shadow_on  = new Button(0, 0, 148, 24, tr(STR_CONFIG_SHADOW_ON));
        m_btn_shadow_off = new Button(0, 0, 148, 24, tr(STR_CONFIG_SHADOW_OFF));
        m_btn_shadow_on->set_on_click([](Button*)  { theme_set_window_shadow(true);  });
        m_btn_shadow_off->set_on_click([](Button*) { theme_set_window_shadow(false); });
        shadow_row->add(m_btn_shadow_on);
        shadow_row->add(m_btn_shadow_off);
        stack->add(shadow_row);

        // ---------- Cor da taskbar ----------
        stack->add(new Label(0, 0, tr(STR_CONFIG_TASKBAR_COLOR)));

        HStack* tb_row = new HStack(0, 0, 24, 6);
        m_btn_tb[0] = new Button(0, 0, 66, 24, tr(STR_CONFIG_TASKBAR_AUTO));
        m_btn_tb[1] = new Button(0, 0, 66, 24, tr(STR_CONFIG_TASKBAR_DARK));
        m_btn_tb[2] = new Button(0, 0, 66, 24, tr(STR_CONFIG_TASKBAR_BLUE));
        m_btn_tb[3] = new Button(0, 0, 66, 24, tr(STR_CONFIG_TASKBAR_PINK));
        m_btn_tb[4] = new Button(0, 0, 66, 24, tr(STR_CONFIG_TASKBAR_GREEN));
        for (int i = 0; i < TASKBAR_STYLE_COUNT; i++) {
            m_btn_tb[i]->set_tag(i);
            m_btn_tb[i]->set_on_click([](Button* self) {
                theme_set_taskbar_style(self->tag());
            });
            tb_row->add(m_btn_tb[i]);
        }
        stack->add(tb_row);

        add(stack);
    }

    void draw(int32_t ox, int32_t oy) override {
        // ---- Wallpaper ----
        int cur = wallpaper_get();
        m_btn_default->set_active_style(cur == -1);
        for (int i = 0; i < m_wallpaper_count; i++) {
            m_btn_wallpaper[i]->set_active_style(cur == m_wallpaper_index[i]);
        }
        WallpaperMode mode = wallpaper_get_mode();
        m_btn_fill->set_active_style(mode == WALLPAPER_MODE_FILL);
        m_btn_center->set_active_style(mode == WALLPAPER_MODE_CENTER);
        m_btn_tile->set_active_style(mode == WALLPAPER_MODE_TILE);

        // ---- Tema ----
        int cur_preset = theme_current_preset();
        for (int i = 0; i < THEME_PRESET_COUNT; i++) {
            m_btn_theme[i]->set_active_style(i == cur_preset);
        }

        // ---- Formas ----
        bool win_round = theme_get_window_rounded();
        m_btn_corners_round->set_active_style(win_round);
        m_btn_corners_square->set_active_style(!win_round);

        bool btn_round = theme_get_buttons_rounded();
        m_btn_buttons_round->set_active_style(btn_round);
        m_btn_buttons_square->set_active_style(!btn_round);

        // ---- Sombra ----
        bool shadow = theme_get_window_shadow();
        m_btn_shadow_on->set_active_style(shadow);
        m_btn_shadow_off->set_active_style(!shadow);

        // ---- Taskbar ----
        int tb_style = theme_get_taskbar_style();
        for (int i = 0; i < TASKBAR_STYLE_COUNT; i++) {
            m_btn_tb[i]->set_active_style(i == tb_style);
        }

        Panel::draw(ox, oy);
    }

private:
    static constexpr int MAX_WALLPAPER_BTNS = 6;

    Button* m_btn_default;
    Button* m_btn_wallpaper[MAX_WALLPAPER_BTNS];
    int     m_wallpaper_index[MAX_WALLPAPER_BTNS];
    int     m_wallpaper_count = 0;
    Button* m_btn_fill;
    Button* m_btn_center;
    Button* m_btn_tile;

    Button* m_btn_theme[THEME_PRESET_COUNT];
    Button* m_btn_corners_round;
    Button* m_btn_corners_square;
    Button* m_btn_buttons_round;
    Button* m_btn_buttons_square;
    Button* m_btn_shadow_on;
    Button* m_btn_shadow_off;
    Button* m_btn_tb[TASKBAR_STYLE_COUNT];
};

// ---- Painel: Sobre o Sistema (hardware) ----
class AboutSystemPanel : public Panel {
public:
    AboutSystemPanel(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : Panel(x, y, w, h, false)
    {
        VStack* stack = new VStack(16, 16, w - 32);
        stack->add(new Label(0, 0, tr(STR_CONFIG_CATEGORY_ABOUT)));

        char buf[64];
        stack->add(make_info_row(tr(STR_CONFIG_CPU), sysinfo_cpu_name()));

        kuitoa((uint64_t)sysinfo_cpu_cores(), buf);
        stack->add(make_info_row(tr(STR_CONFIG_CORES), buf));

        char ram_buf[24];
        sysinfo_format_ram(sysinfo_total_ram(), ram_buf);
        stack->add(make_info_row(tr(STR_CONFIG_RAM_TOTAL), ram_buf));

        sysinfo_format_ram(sysinfo_free_ram(), ram_buf);
        stack->add(make_info_row(tr(STR_CONFIG_RAM_FREE), ram_buf));

        sysinfo_format_ram(mem_get_heap_used(), ram_buf);
        stack->add(make_info_row(tr(STR_CONFIG_HEAP), ram_buf));

        add(stack);
    }

private:
    HStack* make_info_row(const char* key, const char* value) {
        HStack* row = new HStack(0, 0, 18, 8);
        row->add(new Label(0, 0, key));
        row->add(new Label(0, 0, value));
        return row;
    }
};

class ConfigWindow : public Window {
public:
    ConfigWindow(int32_t x, int32_t y, const char* title_)
        : Window(x, y, CFG_W, CFG_H, title_, WinType::Dialog)
    {
        min_width  = 420;
        min_height = 320;

        int32_t content_offset_x = (int32_t)Window::BORDER;
        int32_t content_offset_y = (int32_t)Window::BORDER + (int32_t)Window::TITLE_BAR_H + 1;

        m_nav = new SidebarNav(content_offset_x, content_offset_y,
                               SIDEBAR_W, CFG_H - (uint32_t)content_offset_y - Window::BORDER);
        m_nav->add_item(tr(STR_CONFIG_CATEGORY_SYSTEM));
        m_nav->add_item(tr(STR_CONFIG_CATEGORY_PERSONALIZATION));
        m_nav->add_item(tr(STR_CONFIG_CATEGORY_ABOUT));
        m_nav->set_on_select(&ConfigWindow::on_nav_select, this);
        add(m_nav);

        uint32_t panel_w = CFG_W - (uint32_t)Window::BORDER * 2 - SIDEBAR_W;
        uint32_t panel_h = CFG_H - (uint32_t)content_offset_y - Window::BORDER;
        int32_t  panel_x = content_offset_x + (int32_t)SIDEBAR_W;

        m_system = new SystemPanel(panel_x, content_offset_y, panel_w, panel_h);
        m_pers   = new PersonalizationPanel(panel_x, content_offset_y, panel_w, panel_h);
        m_about  = new AboutSystemPanel(panel_x, content_offset_y, panel_w, panel_h);

        add(m_system);
        add(m_pers);
        add(m_about);

        show_category(CAT_SYSTEM);
    }

    void on_resized() override {
        Rect content = content_area_absolute();
        uint32_t panel_w = content.w - SIDEBAR_W;
        uint32_t panel_h = content.h;

        m_nav->bounds.h = panel_h;
        m_system->bounds.w = panel_w; m_system->bounds.h = panel_h;
        m_pers->bounds.w   = panel_w; m_pers->bounds.h   = panel_h;
        m_about->bounds.w  = panel_w; m_about->bounds.h  = panel_h;
        Window::on_resized();
    }

    void show_category(int index) {
        m_system->visible = (index == CAT_SYSTEM);
        m_pers->visible   = (index == CAT_PERSONALIZATION);
        m_about->visible  = (index == CAT_ABOUT);
    }

    static void on_nav_select(int index, void* user_data) {
        ConfigWindow* self = static_cast<ConfigWindow*>(user_data);
        self->show_category(index);
    }

private:
    SidebarNav*           m_nav;
    SystemPanel*           m_system;
    PersonalizationPanel*  m_pers;
    AboutSystemPanel*      m_about;
};

static ConfigWindow* cfg_win = nullptr;

void open_config_window(void) {
    if (cfg_win && cfg_win->active) {
        cfg_win->minimized = false;
        wm_focus(cfg_win);
        return;
    }

    uint32_t sw = fb_width(), sh = fb_height();

    // Centraliza, mas clampa para dentro da tela — com CFG_H = 580,
    // se o usuário estiver numa resolução baixa (ex.: 800x600), a
    // janela ficaria parcialmente fora da tela se só centralizasse.
    int32_t wx = (int32_t)(sw / 2) - CFG_W / 2;
    int32_t wy = (int32_t)(sh / 2) - CFG_H / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    if ((uint32_t)(wx + CFG_W) > sw) wx = (int32_t)sw - CFG_W;
    if ((uint32_t)(wy + CFG_H) > sh) wy = (int32_t)sh - CFG_H;

    cfg_win = new ConfigWindow(wx, wy, tr(STR_CONFIG_WINDOW_TITLE));
    wm_register(cfg_win);
}