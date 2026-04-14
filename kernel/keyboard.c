#include "keyboard.h"
#include "types.h"

// Scancode set 1: índice = scancode, valor = ASCII
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

#define KB_BUF_SIZE 64
static uint8_t kb_buf[KB_BUF_SIZE];
static uint8_t kb_head = 0;
static uint8_t kb_tail = 0;

static bool shift_pressed = false;
static bool caps_lock     = false;

extern volatile uint8_t  kb_scancode;
extern volatile uint8_t  kb_ready;

void keyboard_init(void) {
    kb_head = kb_tail = 0;
    shift_pressed = caps_lock = false;
}

// Processa um scancode e insere o caractere no buffer
static void process_scancode(uint8_t sc) {
    if (sc == 0xE0) return; // scancode estendido: ignora por ora

    bool release = (sc & 0x80) != 0;
    sc &= 0x7F;

    // teclas modificadoras
    if (sc == 0x2A || sc == 0x36) { // Shift L/R
        shift_pressed = !release;
        return;
    }
    if (sc == 0x3A && !release) { // Caps Lock
        caps_lock = !caps_lock;
        return;
    }

    if (release) return;

    // converte para ASCII
    uint8_t c = shift_pressed ? sc_shift[sc] : sc_normal[sc];
    if (!c) return;

    // aplica caps lock a letras
    if (caps_lock) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }

    // insere no buffer circular
    uint8_t next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

void keyboard_poll(void) {
    // 1) Processa via IRQ (se houver)
    if (kb_ready) {
        kb_ready = 0;
        process_scancode(kb_scancode);
    }

    // 2) Fallback: polling direto do controlador PS/2 (útil se IRQ falhar)
    while (inb(0x64) & 0x01) {   // bit 0 = output buffer cheio
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