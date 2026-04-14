# ============================================================
#  HAOS v1.1  —  Makefile
# ============================================================

CROSS   ?= x86_64-linux-gnu
CC       = $(CROSS)-gcc
LD       = $(CROSS)-ld
NASM     = nasm
GRUB     = grub-mkrescue
QEMU     = qemu-system-x86_64

CFLAGS   = -ffreestanding -fno-stack-protector -fno-pic        \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2           \
           -nostdlib -nodefaultlibs                             \
           -O2 -Wall -Wextra                                    \
           -Wno-override-init -Wno-unused-variable             \
           -I. -Ikernel -Idrivers -Igui                        \
           -m64 -std=c11

LDFLAGS  = -T linker.ld -nostdlib -z max-page-size=0x1000
NASMFLAGS = -f elf64

# ---- Fix crítico: desabilita PipeWire/PulseAudio no QEMU 8.x ----
QEMU_AUDIO   = -audiodev none,id=noaudio
QEMU_MACHINE = -machine pc,accel=tcg
QEMU_BASE    = -m 256M -vga std -no-reboot $(QEMU_AUDIO) $(QEMU_MACHINE)

# ---- Mouse PS/2 no QEMU: precisa de -usbdevice tablet OU deixar
#      o PS/2 padrão (que o QEMU emula automaticamente via i8042)
QEMU_INPUT   = -device ps2-mouse

ASM_SRCS = boot/boot.asm          \
           kernel/gdt_asm.asm     \
           kernel/idt_asm.asm

C_SRCS   = kernel/kernel.c        \
           kernel/gdt.c           \
           kernel/idt.c           \
           kernel/pic.c           \
           kernel/pit.c           \
           kernel/keyboard.c      \
           kernel/memory.c        \
           drivers/fb.c           \
           drivers/font.c         \
           drivers/mouse.c        \
           gui/window.c           \
           gui/terminal.c         \
           gui/gui.c

ASM_OBJS = $(ASM_SRCS:.asm=.o)
C_OBJS   = $(C_SRCS:.c=.o)
OBJS     = $(ASM_OBJS) $(C_OBJS)

.PHONY: all iso run run-gtk run-sdl run-vnc run-elf debug clean

all: haos.elf

%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

haos.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "  [OK] haos.elf gerado"

iso: haos.elf
	cp haos.elf iso/boot/haos.elf
	$(GRUB) -o haos.iso iso 2>/dev/null
	@echo "  [OK] haos.iso gerado"

# GTK (WSL2 com WSLg — melhor opção visual)
run-gtk: iso
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display gtk

# SDL sem OpenGL
run-sdl: iso
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display sdl,gl=off

# VNC headless
run-vnc: iso
	@echo ""
	@echo "  ============================================="
	@echo "  QEMU VNC em localhost:5900"
	@echo "  Use TigerVNC Viewer no Windows"
	@echo "  ============================================="
	@echo ""
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display none -vnc :0

run: iso
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display gtk 2>/dev/null || \
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display sdl,gl=off 2>/dev/null || \
	$(MAKE) run-vnc

run-elf: haos.elf
	$(QEMU) -kernel haos.elf $(QEMU_BASE) $(QEMU_INPUT) -display gtk 2>/dev/null || \
	$(QEMU) -kernel haos.elf $(QEMU_BASE) $(QEMU_INPUT) -display sdl,gl=off 2>/dev/null || \
	$(QEMU) -kernel haos.elf $(QEMU_BASE) $(QEMU_INPUT) -display none -vnc :0

debug: iso
	@echo "  [GDB] Conecte: gdb haos.elf → target remote :1234"
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) \
	        -display none -vnc :0 -s -S &

clean:
	find . -name "*.o" -delete
	rm -f haos.elf haos.iso iso/boot/haos.elf
	@echo "  [OK] limpeza concluída"
