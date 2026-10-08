// Teste de host da tradução HID -> scancodes (drivers/usb_hid.c). Só compila
// usb_hid.c + este arquivo; o teclado/mouse reais são substituídos por stubs.
#include <stdio.h>
#include <string.h>
#include "../../kernel/types.h"
#include "../../drivers/usb_hid.h"

volatile uint64_t timer_ticks = 1000;
static uint8_t g_out[64]; static int g_n;
static int g_mdx, g_mdy, g_mbtn, g_mwheel;
void keyboard_inject_scancode(uint8_t sc) { if (g_n < 64) g_out[g_n++] = sc; }
void mouse_inject(int32_t dx, int32_t dy, uint8_t b, int32_t w) { g_mdx = dx; g_mdy = dy; g_mbtn = b; g_mwheel = w; }

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FALHOU: %s (linha %d)\n", #c, __LINE__); } } while (0)
static bool out_is(const uint8_t* e, int n) { return g_n == n && memcmp(g_out, e, (size_t)n) == 0; }
static void clear(void) { g_n = 0; }

int main(void) {
    uint8_t r[8];

    printf("[1] tecla 'a': make e break\n");
    memset(r, 0, 8); r[2] = 0x04; usb_hid_keyboard_report(r, 8);
    { uint8_t e[] = {0x1E}; CHECK(out_is(e, 1)); }
    clear(); memset(r, 0, 8); usb_hid_keyboard_report(r, 8);
    { uint8_t e[] = {0x9E}; CHECK(out_is(e, 1)); }

    printf("[2] Shift esquerdo + 'a'\n");
    clear(); memset(r, 0, 8); r[0] = 0x02; r[2] = 0x04; usb_hid_keyboard_report(r, 8);
    { uint8_t e[] = {0x2A, 0x1E}; CHECK(out_is(e, 2)); }
    clear(); memset(r, 0, 8); usb_hid_keyboard_report(r, 8);
    { uint8_t e[] = {0x9E, 0xAA}; CHECK(out_is(e, 2)); }

    printf("[3] seta para cima = E0 48 (e E0 C8 ao soltar)\n");
    clear(); memset(r, 0, 8); r[2] = 0x52; usb_hid_keyboard_report(r, 8);
    { uint8_t e[] = {0xE0, 0x48}; CHECK(out_is(e, 2)); }
    clear(); memset(r, 0, 8); usb_hid_keyboard_report(r, 8);
    { uint8_t e[] = {0xE0, 0xC8}; CHECK(out_is(e, 2)); }

    printf("[4] ErrorRollOver e relatorio curto sao ignorados\n");
    clear(); memset(r, 0, 8); r[2] = 0x01; usb_hid_keyboard_report(r, 8);
    CHECK(g_n == 0);
    r[2] = 0x04; usb_hid_keyboard_report(r, 4);
    CHECK(g_n == 0);

    printf("[5] repeticao: so depois de 500 ms, depois a cada 30 ms\n");
    clear(); memset(r, 0, 8); r[2] = 0x05; usb_hid_keyboard_report(r, 8);   // 'b' (0x30)
    clear();
    timer_ticks += 49; usb_hid_tick(); CHECK(g_n == 0);
    timer_ticks += 1;  usb_hid_tick(); CHECK(g_n == 1 && g_out[0] == 0x30);
    timer_ticks += 3;  usb_hid_tick(); CHECK(g_n == 2);
    memset(r, 0, 8); usb_hid_keyboard_report(r, 8);                          // solta
    clear(); timer_ticks += 100; usb_hid_tick(); CHECK(g_n == 0);

    printf("[6] teclado desconectado solta tudo\n");
    clear(); memset(r, 0, 8); r[0] = 0x01; r[2] = 0x04; usb_hid_keyboard_report(r, 8);
    clear(); usb_hid_keyboard_reset();
    { uint8_t e[] = {0x9E, 0x9D}; CHECK(out_is(e, 2)); }

    printf("[7] mouse\n");
    uint8_t m[3] = { 0x01, 5, (uint8_t)-3 };
    usb_hid_mouse_report(m, 3);
    CHECK(g_mdx == 5 && g_mdy == -3 && g_mbtn == 1 && g_mwheel == 0);

    printf("\n%d verificacoes, %d falha(s)\n", checks, fails);
    return fails ? 1 : 0;
}
