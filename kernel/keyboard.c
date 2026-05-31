#include "keyboard.h"
#include "types.h"

// Scancode tables (normal e shift)
static const uint8_t sc_normal[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0, // F1-F10
    0,0,  // Num lock, Scroll lock
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,
    0,0   // F11, F12
};

static const uint8_t sc_shift[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0,  ' ', 0
};

// Códigos especiais para setas
#define KEY_UP   0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83

// Buffer de caracteres decodificados (para a aplicação)
#define KB_BUF_SIZE 64
static uint8_t kb_char_buf[KB_BUF_SIZE];
static uint8_t kb_char_head = 0;
static uint8_t kb_char_tail = 0;

// Variáveis exportadas pela ISR (assembly) – buffer circular
extern volatile uint8_t keyboard_buf[32];
extern volatile uint8_t keyboard_hd;
extern volatile uint8_t keyboard_tl;

static bool shift_pressed = false;
static bool caps_lock     = false;
static bool extended_mode = false;

static void kb_push_char(uint8_t c) {
    uint8_t next = (kb_char_head + 1) % KB_BUF_SIZE;
    if (next != kb_char_tail) {
        kb_char_buf[kb_char_head] = c;
        kb_char_head = next;
    }
}

static void process_scancode(uint8_t sc) {
    // Prefixo de scancode estendido
    if (sc == 0xE0) {
        extended_mode = true;
        return;
    }

    bool release = (sc & 0x80) != 0;
    sc &= 0x7F;

    if (extended_mode) {
        extended_mode = false;
        if (!release) {
            switch (sc) {
                case 0x48: kb_push_char(KEY_UP);    return;
                case 0x50: kb_push_char(KEY_DOWN);  return;
                case 0x4B: kb_push_char(KEY_LEFT);  return;
                case 0x4D: kb_push_char(KEY_RIGHT); return;
            }
        }
        return;
    }

    // Modificadores
    if (sc == 0x2A || sc == 0x36) { shift_pressed = !release; return; }
    if (sc == 0x3A && !release)   { caps_lock = !caps_lock;   return; }
    if (release) return;

    uint8_t c = shift_pressed ? sc_shift[sc] : sc_normal[sc];
    if (!c) return;

    if (caps_lock) {
        if      (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }

    kb_push_char(c);
}

void keyboard_init(void) {
    kb_char_head = kb_char_tail = 0;
    shift_pressed = caps_lock = extended_mode = false;
    // Zera os cabeçalhos do buffer circular da ISR
    keyboard_hd = 0;
    keyboard_tl = 0;
}

void keyboard_poll(void) {
    // Processa todos os scancodes pendentes no ring buffer da ISR
    while (keyboard_tl != keyboard_hd) {
        uint8_t sc = keyboard_buf[keyboard_tl];
        keyboard_tl = (keyboard_tl + 1) & 31;
        process_scancode(sc);
    }
}

uint8_t keyboard_getchar(void) {
    keyboard_poll();
    if (kb_char_head == kb_char_tail) return 0;
    uint8_t c = kb_char_buf[kb_char_tail];
    kb_char_tail = (kb_char_tail + 1) % KB_BUF_SIZE;
    return c;
}

bool keyboard_has_data(void) {
    keyboard_poll();
    return kb_char_head != kb_char_tail;
}