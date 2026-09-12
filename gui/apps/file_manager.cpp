#include "file_manager.h"
#include "file_manager_fs.h"
#include "editor.h"
#include "../core/wm.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"
#include "../../kernel/lang.h"

extern "C" volatile uint64_t timer_ticks;

namespace {

constexpr int32_t PAD = 10;
constexpr int32_t TOOLBAR_H = 34;
constexpr int32_t TOOLBAR_BUTTON_W = 58;
constexpr int32_t TOOLBAR_GAP = 6;
constexpr int32_t HEADER_H = 22;
constexpr int32_t ROW_H = 24;
constexpr int32_t STATUS_H = 22;
constexpr uint64_t DOUBLE_CLICK_TICKS = 35;

class FileManagerWindow : public Window {
public:
    VfsNode* current_dir;
    VfsNode* selected;
    VfsNode* last_clicked;
    uint64_t last_click_tick;
    char status[96];

    FileManagerWindow(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* title)
        : Window(x, y, w, h, title, WinType::Generic),
          current_dir(vfs_root()), selected(nullptr), last_clicked(nullptr),
          last_click_tick(0) {
        status[0] = 0;
        min_width = 360;
        min_height = 220;
    }

    void set_status(const char* text) {
        kstrcpy(status, text);
    }

    void draw(int32_t ox, int32_t oy) override {
        kstrncpy(title, tr(STR_FILE_MANAGER_TITLE), sizeof(title) - 1);
        title[sizeof(title) - 1] = 0;
        Window::draw(ox, oy);
        if (!active || minimized) return;

        Rect content = content_area_absolute();
        int32_t left = content.x + PAD;
        int32_t top = content.y + PAD;
        int32_t width = (int32_t)content.w - PAD * 2;
        if (width < 20) return;

        fb_fill_rect((uint32_t)left, (uint32_t)top, (uint32_t)width,
                     TOOLBAR_H, 0x142742);
        bool can_go_back = current_dir != vfs_root();
        uint32_t button_color = can_go_back ? 0x244A72 : 0x1B2D46;
        fb_fill_rect((uint32_t)left, (uint32_t)(top + 4),
                 TOOLBAR_BUTTON_W, TOOLBAR_H - 8, button_color);
        fb_draw_string_centered((uint32_t)left, (uint32_t)(top + 4),
                    TOOLBAR_BUTTON_W, TOOLBAR_H - 8,
                    tr(STR_FILE_MANAGER_BACK),
                    can_go_back ? COLOR_TEXT_LIGHT : COLOR_TEXT_GRAY,
                    0, true);

        int32_t home_x = left + TOOLBAR_BUTTON_W + TOOLBAR_GAP;
        fb_fill_rect((uint32_t)home_x, (uint32_t)(top + 4),
                 TOOLBAR_BUTTON_W, TOOLBAR_H - 8, 0x244A72);
        fb_draw_string_centered((uint32_t)home_x, (uint32_t)(top + 4),
                    TOOLBAR_BUTTON_W, TOOLBAR_H - 8,
                    tr(STR_FILE_MANAGER_HOME), COLOR_TEXT_LIGHT,
                    0, true);

        char path[512];
        file_manager_path(current_dir, path, sizeof(path));
        fb_draw_string((uint32_t)(home_x + TOOLBAR_BUTTON_W + TOOLBAR_GAP),
                   (uint32_t)(top + 9),
                       path, COLOR_TEXT_LIGHT, 0, true);

        int32_t header_top = top + TOOLBAR_H + 6;
        int32_t list_top = header_top + HEADER_H;
        int32_t list_bottom = content.y + (int32_t)content.h - STATUS_H - PAD;
        int32_t visible_height = list_bottom - list_top;
        if (visible_height < 0) visible_height = 0;
        uint32_t visible_rows = (uint32_t)(visible_height / ROW_H);

        fb_fill_rect((uint32_t)left, (uint32_t)header_top, (uint32_t)width,
                 HEADER_H, 0x1B3554);
        fb_draw_string((uint32_t)(left + 8), (uint32_t)(header_top + 3),
                   tr(STR_FILE_MANAGER_NAME), COLOR_TEXT_LIGHT, 0, true);
        fb_draw_string((uint32_t)(left + width - 160),
                   (uint32_t)(header_top + 3),
                   tr(STR_FILE_MANAGER_TYPE), COLOR_TEXT_LIGHT, 0, true);
        fb_draw_string((uint32_t)(left + width - 68),
                   (uint32_t)(header_top + 3),
                   tr(STR_FILE_MANAGER_SIZE), COLOR_TEXT_LIGHT, 0, true);

        for (uint32_t i = 0; i < current_dir->child_count && i < visible_rows; i++) {
            VfsNode* child = file_manager_child(current_dir, i);
            if (!child) continue;
            int32_t row_y = list_top + (int32_t)i * ROW_H;
            if (child == selected)
                fb_fill_rect((uint32_t)left, (uint32_t)row_y, (uint32_t)width,
                             ROW_H - 2, 0x244A72);

            const char* marker = child->type == VFS_DIR
                ? tr(STR_TERM_STAT_TYPE_DIR) : tr(STR_TERM_STAT_TYPE_FILE);
            uint32_t marker_color = child->type == VFS_DIR ? COLOR_ACCENT : COLOR_TEXT_GRAY;
            fb_draw_string((uint32_t)(left + width - 160),
                           (uint32_t)(row_y + 5), marker, marker_color, 0, true);
            fb_draw_string((uint32_t)(left + 8), (uint32_t)(row_y + 5),
                           child->name, COLOR_TEXT_LIGHT, 0, true);

            if (child->type == VFS_FILE) {
                char size_text[24];
                kitoa((int64_t)child->size, size_text);
                int32_t size_x = left + width - 68;
                fb_draw_string((uint32_t)size_x, (uint32_t)(row_y + 5),
                               size_text, COLOR_TEXT_GRAY, 0, true);
            }
        }

        if (current_dir->child_count == 0) {
            fb_draw_string((uint32_t)(left + 8), (uint32_t)(list_top + 5),
                           tr(STR_FILE_MANAGER_EMPTY), COLOR_TEXT_GRAY, 0, true);
        }
        if (status[0]) {
            fb_draw_string((uint32_t)left,
                           (uint32_t)(content.y + (int32_t)content.h - FONT_H - PAD),
                           status, COLOR_TEXT_GRAY, 0, true);
        }
    }

    void open_selected(VfsNode* node) {
        if (!node) return;
        if (node->type == VFS_DIR) {
            current_dir = node;
            selected = nullptr;
            last_clicked = nullptr;
            status[0] = 0;
            return;
        }
        if (!file_manager_is_text(node)) {
            set_status(tr(STR_FILE_MANAGER_BINARY));
            return;
        }

        char path[512];
        file_manager_path(node, path, sizeof(path));
        uint32_t sw = fb_width(), sh = fb_height();
        editor_create((int32_t)(sw / 2 - 300), (int32_t)(sh / 2 - 200), path);
        set_status(tr(STR_FILE_MANAGER_OPENED));
    }

    EventResult on_event(const WidgetEvent& ev) override {
        if (ev.type == EventType::KeyDown) {
            if (ev.key == 27) {
                wm_close(this);
                return EventResult::Handled;
            }
            if (ev.key == '\b' || ev.key == 0x08) {
                current_dir = file_manager_parent(current_dir);
                selected = nullptr;
                return EventResult::Handled;
            }
            return EventResult::Handled;
        }
        if (ev.type != EventType::MouseDown) return EventResult::Ignored;

        Rect content = content_area_absolute();
        int32_t mx = ev.x;
        int32_t my = ev.y;
        if (!content.contains(mx, my)) return EventResult::Ignored;

        int32_t left = content.x + PAD;
        int32_t top = content.y + PAD;
        int32_t home_x = left + TOOLBAR_BUTTON_W + TOOLBAR_GAP;
        if (my >= top + 4 && my < top + TOOLBAR_H - 4 &&
            mx >= left && mx < left + TOOLBAR_BUTTON_W) {
            current_dir = file_manager_parent(current_dir);
            selected = nullptr;
            last_clicked = nullptr;
            status[0] = 0;
            return EventResult::Handled;
        }
        if (my >= top + 4 && my < top + TOOLBAR_H - 4 &&
            mx >= home_x && mx < home_x + TOOLBAR_BUTTON_W) {
            current_dir = vfs_root();
            selected = nullptr;
            last_clicked = nullptr;
            status[0] = 0;
            return EventResult::Handled;
        }

        int32_t list_top = top + TOOLBAR_H + 6 + HEADER_H;
        if (my < list_top) return EventResult::Handled;
        int32_t row = (my - list_top) / ROW_H;
        if (row < 0 || (uint32_t)row >= current_dir->child_count) {
            selected = nullptr;
            return EventResult::Handled;
        }

        VfsNode* node = file_manager_child(current_dir, (uint32_t)row);
        uint64_t now = timer_ticks;
        bool double_click = node && node == last_clicked &&
                    now >= last_click_tick &&
                    now - last_click_tick <= DOUBLE_CLICK_TICKS;
        selected = node;
        last_clicked = node;
        last_click_tick = now;
        if (double_click) open_selected(node);
        return EventResult::Handled;
    }
};

} // namespace

Window* file_manager_create(int32_t x, int32_t y) {
    FileManagerWindow* win = new FileManagerWindow(x, y, 620, 440,
                                                    tr(STR_FILE_MANAGER_TITLE));
    if (!wm_register(win)) {
        delete win;
        return nullptr;
    }
    return win;
}
