// drivers/mouse.c — driver PS/2 para mouse (IRQ12)
#include "mouse.h"
#include "../kernel/types.h"

// ---- Variáveis do ring-buffer preenchido pela ISR (idt_asm.asm) ----
extern volatile uint8_t mouse_buf[32];  // ring buffer de bytes crus
extern volatile uint8_t mouse_hd;       // head (escrito pela ISR)
extern volatile uint8_t mouse_tl;       // tail (lido por nós)

// ---- Helpers de I/O PS/2 ----------------------------------------

static void ps2_wait_write(void) {
    int t = 100000;
    while (t-- && (inb(0x64) & 0x02));
}

static void ps2_wait_read(void) {
    int t = 100000;
    while (t-- && !(inb(0x64) & 0x01));
}

static void mouse_send(uint8_t cmd) {
    ps2_wait_write();
    outb(0x64, 0xD4);   // "próximo byte vai para o mouse"
    ps2_wait_write();
    outb(0x60, cmd);
}

static uint8_t mouse_recv(void) {
    ps2_wait_read();
    return inb(0x60);
}

// ---- Estado interno do mouse ------------------------------------

static int32_t mx = 512, my = 384;
static int32_t bounds_x = 1023, bounds_y = 767;

// Packet parser (3 bytes por evento padrão)
static uint8_t  pkt[3];
static int      pkt_phase = 0;

// Botões: dois snapshots para detecção de click
static uint8_t  btn_now  = 0;   // atualizado a cada pacote
static uint8_t  btn_prev = 0;   // snapshot anterior (antes do snap)
static uint8_t  btn_snap = 0;   // snapshot do frame atual

// ---- Inicialização ----------------------------------------------

void mouse_init(void) {
    // Habilita porta auxiliar
    ps2_wait_write();
    outb(0x64, 0xA8);

    // Lê e modifica byte de configuração: habilita IRQ12, clock do mouse
    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t cfg = inb(0x60);
    cfg |=  0x02;   // IRQ12 enable
    cfg &= ~0x20;   // habilita clock do mouse
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, cfg);

    // Restaura padrões e habilita data reporting
    mouse_send(0xF6);  mouse_recv();  // set defaults
    mouse_send(0xF4);  mouse_recv();  // enable reporting

    pkt_phase = 0;
    mx = 512; my = 384;
}

// ---- Processamento do ring-buffer (polling) ---------------------

void mouse_process(void) {
    while (mouse_tl != mouse_hd) {
        uint8_t b = mouse_buf[mouse_tl];
        mouse_tl = (mouse_tl + 1) & 31;

        switch (pkt_phase) {
            case 0:
                // Bit 3 deve estar setado no byte de status
                if (b & 0x08) {
                    pkt[0] = b;
                    pkt_phase = 1;
                }
                break;
            case 1:
                pkt[1] = b;
                pkt_phase = 2;
                break;
            case 2:
                pkt[2] = b;
                pkt_phase = 0;

                // Extrai botões
                btn_now = pkt[0] & 0x07;

                // Extrai delta com extensão de sinal
                int16_t dx = (int16_t)pkt[1];
                int16_t dy = (int16_t)pkt[2];
                if (pkt[0] & 0x10) dx |= (int16_t)0xFF00;
                if (pkt[0] & 0x20) dy |= (int16_t)0xFF00;

                mx += (int32_t)dx;
                my -= (int32_t)dy;  // eixo Y invertido

                // Clampa nos limites
                if (mx < 0) mx = 0;
                if (my < 0) my = 0;
                if (mx > bounds_x) mx = bounds_x;
                if (my > bounds_y) my = bounds_y;
                break;
        }
    }
}

// ---- API pública ------------------------------------------------

void mouse_snap(void) {
    btn_prev = btn_snap;
    btn_snap = btn_now;
}

void mouse_set_bounds(int32_t bx, int32_t by) {
    bounds_x = bx;
    bounds_y = by;
    if (mx > bounds_x) mx = bounds_x;
    if (my > bounds_y) my = bounds_y;
}

int32_t mouse_get_x(void) { return mx; }
int32_t mouse_get_y(void) { return my; }

bool mouse_left_pressed(void)  { return (btn_snap & 1) != 0; }
bool mouse_left_clicked(void)  { return (btn_snap & 1) && !(btn_prev & 1); }
bool mouse_right_clicked(void) { return (btn_snap & 2) && !(btn_prev & 2); }
