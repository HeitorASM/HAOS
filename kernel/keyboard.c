#include "keyboard.h"
#include "types.h"


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

// Códigos especiais emitidos para o terminal
#define KEY_UP   0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83

#define KB_BUF_SIZE 64
static uint8_t kb_buf[KB_BUF_SIZE];
static uint8_t kb_head = 0;
static uint8_t kb_tail = 0;

static bool shift_pressed  = false;
static bool caps_lock      = false;
static bool extended_mode  = false;  // recebeu 0xE0

extern volatile uint8_t  kb_scancode;
extern volatile uint8_t  kb_ready;

void keyboard_init(void) {
    kb_head = kb_tail = 0;
    shift_pressed = caps_lock = extended_mode = false;
}

static void kb_push(uint8_t c) {
    uint8_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
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
                case 0x48: kb_push(KEY_UP);    return; // seta cima
                case 0x50: kb_push(KEY_DOWN);  return; // seta baixo
                case 0x4B: kb_push(KEY_LEFT);  return; // seta esquerda
                case 0x4D: kb_push(KEY_RIGHT); return; // seta direita
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

    kb_push(c);
}

void keyboard_poll(void) {
    if (kb_ready) {
        kb_ready = 0;
        process_scancode(kb_scancode);
    }
    while (inb(0x64) & 0x01) {
        uint8_t sc = inb(0x60);
        process_scancode(sc);
    }
}

uint8_t keyboard_getchar(void) {
    keyboard_poll();
    if (kb_head == kb_tail) return 0;
    uint8_t c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return c;
}

bool keyboard_has_data(void) {
    keyboard_poll();
    return kb_head != kb_tail;
}
