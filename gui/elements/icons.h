// gui/elements/icons.h
#pragma once
#include "../../kernel/types.h"   // <-- substitui <stdint.h>

#define ICON_W        56
#define ICON_H        48
#define ICON_LABEL_H  14

#define ICON_TERM_X   20
#define ICON_TERM_Y   20
#define ICON_ABOUT_X  20
#define ICON_ABOUT_Y  (20 + ICON_H + ICON_LABEL_H + 16)
#define ICON_CONF_X   20
#define ICON_CONF_Y   (20 + (ICON_H + ICON_LABEL_H + 16) * 2)

void draw_icon_terminal(uint32_t ix, uint32_t iy);
void draw_icon_about(uint32_t ix, uint32_t iy);
void draw_icon_settings(uint32_t ix, uint32_t iy);
void draw_desktop_icons(void);