#include "config.h"
#include "../core/wm.h"
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

#define CFG_W   560
#define CFG_H   440
#define SIDEBAR_W 160

enum ConfigCategory { CAT_SYSTEM = 0, CAT_PERSONALIZATION = 1, CAT_ABOUT = 2 };

// ---- Painel: Sistema (idioma) ----
class SystemPanel : public Panel {
public:
    SystemPanel(int32_t x, int32_t y, uint32_t w, uint32_t h) : Panel(x, y, w, h, false) {
        VStack* stack = new VStack(16, 16, w - 32);

        Label* title = new Label(0, 0, tr(STR_CONFIG_CATEGORY_SYSTEM));
        stack->add(title);

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

    // Destaca visualmente qual idioma está ativo — chamado a cada
    // draw() já que lang_get() pode mudar por fora (ex.: se algum
    // dia outro app também trocar o idioma).
    void draw(int32_t ox, int32_t oy) override {
        Lang cur = lang_get();
        m_btn_pt->set_active_style(cur == LANG_PT);
        m_btn_en->set_active_style(cur == LANG_EN);
        Panel::draw(ox, oy);
    }

private:
    Button* m_btn_pt;
    Button* m_btn_en;
};

// ---- Painel: Personalização (wallpaper + modo) ----
class PersonalizationPanel : public Panel {
public:
    PersonalizationPanel(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : Panel(x, y, w, h, false)
    {
        VStack* stack = new VStack(16, 16, w - 32);
        stack->add(new Label(0, 0, tr(STR_CONFIG_CATEGORY_PERSONALIZATION)));
        stack->add(new Label(0, 0, tr(STR_CONFIG_WALLPAPER)));

        int count = wallpaper_count();
        m_btn_default = new Button(0, 0, w - 32, 26, tr(STR_CONFIG_DEFAULT_WALLPAPER));
        m_btn_default->set_on_click([](Button*) { wallpaper_set(-1); });
        stack->add(m_btn_default);

        for (int i = 0; i < count && i < MAX_WALLPAPER_BTNS; i++) {
            m_btn_wallpaper[i] = new Button(0, 0, w - 32, 26, wallpaper_name(i));
            m_wallpaper_index[i] = i;

            m_btn_wallpaper[i]->set_tag(i);
            m_btn_wallpaper[i]->set_on_click([](Button* self) {
                wallpaper_set(self->tag());
            });
            stack->add(m_btn_wallpaper[i]);
        }
        m_wallpaper_count = count;

        stack->add(new Label(0, 0, tr(STR_CONFIG_MODE)));
        HStack* mode_row = new HStack(0, 0, 26);
        m_btn_fill   = new Button(0, 0, 108, 26, tr(STR_CONFIG_MODE_FILL));
        m_btn_center = new Button(0, 0, 108, 26, tr(STR_CONFIG_MODE_CENTER));
        m_btn_tile   = new Button(0, 0, 108, 26, tr(STR_CONFIG_MODE_TILE));
        m_btn_fill->set_on_click([](Button*)   { wallpaper_set_mode(WALLPAPER_MODE_FILL); });
        m_btn_center->set_on_click([](Button*) { wallpaper_set_mode(WALLPAPER_MODE_CENTER); });
        m_btn_tile->set_on_click([](Button*)   { wallpaper_set_mode(WALLPAPER_MODE_TILE); });
        mode_row->add(m_btn_fill);
        mode_row->add(m_btn_center);
        mode_row->add(m_btn_tile);
        stack->add(mode_row);

        add(stack);
    }

    void draw(int32_t ox, int32_t oy) override {
        int cur = wallpaper_get();
        m_btn_default->set_active_style(cur == -1);
        for (int i = 0; i < m_wallpaper_count; i++) {
            m_btn_wallpaper[i]->set_active_style(cur == m_wallpaper_index[i]);
        }
        WallpaperMode mode = wallpaper_get_mode();
        m_btn_fill->set_active_style(mode == WALLPAPER_MODE_FILL);
        m_btn_center->set_active_style(mode == WALLPAPER_MODE_CENTER);
        m_btn_tile->set_active_style(mode == WALLPAPER_MODE_TILE);
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
    // "chave: valor" numa linha só, como uma HStack de dois Labels —
    // reaproveita o layout automático em vez de calcular posição X
    // manualmente como a versão anterior fazia (draw_info_row com
    // offset fixo de 120px).
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
    int32_t wx = (int32_t)(sw / 2) - CFG_W / 2;
    int32_t wy = (int32_t)(sh / 2) - CFG_H / 2;

    cfg_win = new ConfigWindow(wx, wy, tr(STR_CONFIG_WINDOW_TITLE));
    wm_register(cfg_win);
}
