# ============================================================
#  HAOS v2.0  —  Makefile (C + C++17 híbrido)
# ============================================================

CROSS   ?= x86_64-linux-gnu
CC       = $(CROSS)-gcc
CXX      = $(CROSS)-g++
LD       = $(CROSS)-ld
NASM     = nasm
GRUB     = grub-mkrescue
QEMU     = qemu-system-x86_64

# ── Auto-detecta wallpapers ───────────────────────────────────────────────
WALLPAPER_SRCS := $(wildcard assets/wallpapers/wallpaper_*.c)
WALLPAPER_OBJS := $(WALLPAPER_SRCS:.c=.o)

ifneq ($(WALLPAPER_SRCS),)
  WALLPAPER_FLAG = -DHAOS_HAS_WALLPAPERS
  $(info [wallpaper] $(words $(WALLPAPER_SRCS)) wallpaper(s) encontrado(s))
else
  WALLPAPER_FLAG =
endif

# ── Flags comuns a C e C++ ───────────────────────────────────────────────
COMMON_FLAGS = \
    -ffreestanding -fno-stack-protector -fno-pic  \
    -mno-red-zone -mno-mmx -mno-sse -mno-sse2     \
    -nostdlib -nodefaultlibs                        \
    -O2 -Wall -Wextra                               \
    -Wno-unused-variable                            \
    -I. -Ikernel -Idrivers -Igui -Ifs               \
    -Igui/screens -Igui/elements -Igui/apps         \
    -m64                                            \
    $(WALLPAPER_FLAG)

# ── Flags exclusivas de C ────────────────────────────────────────────────
CFLAGS = $(COMMON_FLAGS) \
    -std=c11              \
    -Wno-override-init

# ── Flags exclusivas de C++17 ───────────────────────────────────────────
#   -fno-exceptions : sem suporte a try/catch (reduz tamanho do kernel)
#   -fno-rtti       : sem type_info em tempo de execução (reduz overhead)
#   -fno-threadsafe-statics : sem locks para objetos estáticos locais
CXXFLAGS = $(COMMON_FLAGS) \
    -std=c++17              \
    -fno-exceptions         \
    -fno-rtti               \
    -fno-threadsafe-statics \
    -Wno-non-virtual-dtor

LDFLAGS   = -T linker.ld -nostdlib -z max-page-size=0x1000
NASMFLAGS = -f elf64

QEMU_AUDIO   = -audiodev none,id=noaudio
QEMU_MACHINE = -machine pc,accel=tcg
QEMU_BASE    = -m 256M -vga std -no-reboot $(QEMU_AUDIO) $(QEMU_MACHINE) -rtc base=localtime
QEMU_INPUT   = -device ps2-mouse

# ── Fontes Assembly ───────────────────────────────────────────────────────
ASM_SRCS = boot/boot.asm          \
           kernel/gdt_asm.asm     \
           kernel/idt_asm.asm

# ── Fontes C (mantidas em C puro) ────────────────────────────────────────
C_SRCS   = kernel/kernel.c        \
           kernel/gdt.c           \
           kernel/idt.c           \
           kernel/pic.c           \
           kernel/pit.c           \
           kernel/keyboard.c      \
           drivers/fb.c           \
           drivers/utf8cp437.c    \
           drivers/font.c         \
           drivers/mouse.c        \
           drivers/rtc.c          \
           gui/window.c           \
           gui/gui.c              \
           gui/screens/boot.c     \
           gui/screens/welcome.c  \
           gui/screens/desktop.c  \
           gui/elements/icons.c   \
           gui/elements/taskbar.c \
           gui/elements/startmenu.c \
           gui/apps/terminal.c    \
           gui/apps/about.c       \
           gui/apps/config.c      \
           gui/wallpaper.c        \
           fs/vfs.c

# ── Fontes C++ (novos módulos) ────────────────────────────────────────────
CXX_SRCS = kernel/memory.cpp      \
           kernel/sysinfo.cpp     \
           kernel/crt.cpp         \
           gui/widget.cpp

ASM_OBJS     = $(ASM_SRCS:.asm=.o)
C_OBJS       = $(C_SRCS:.c=.o)
CXX_OBJS     = $(CXX_SRCS:.cpp=.o)
OBJS         = $(ASM_OBJS) $(C_OBJS) $(CXX_OBJS) $(WALLPAPER_OBJS)

.PHONY: all iso run run-gtk run-sdl run-vnc run-elf debug clean wallpapers

all: haos.elf

# ── Regras de compilação ──────────────────────────────────────────────────
%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ── Linkagem ──────────────────────────────────────────────────────────────
haos.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "  [OK] haos.elf gerado"

iso: haos.elf
	cp haos.elf iso/boot/haos.elf
	$(GRUB) -o haos.iso iso 2>/dev/null
	@echo "  [OK] haos.iso gerado"

run-gtk: iso
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display gtk

run-sdl: iso
	$(QEMU) -cdrom haos.iso -boot d $(QEMU_BASE) $(QEMU_INPUT) -display sdl,gl=off

run-vnc:
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

wallpapers:
	python3 tools/img2wallpaper.py assets/ --width 1024 --height 768
	@echo ""
	@echo "  Recompilando com os novos wallpapers..."
	$(MAKE) all
