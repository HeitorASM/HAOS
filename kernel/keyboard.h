#pragma once
#include "types.h"
void     keyboard_init(void);
void     keyboard_poll(void);
uint8_t  keyboard_getchar(void);
bool     keyboard_has_data(void);
