#pragma once
#include <stdint.h>

#define TASKBAR_H 32

void taskbar_draw(void);
/* Returns 1 if start menu should toggle (key S or Enter on focused button) */
int  taskbar_start_pressed(int key);

void start_menu_draw(int visible);
/* Returns 1 if a menu item was selected. Sets item_index. */
int  start_menu_handle(int key, int* item);
