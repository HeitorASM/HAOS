# libc/Makefile — biblioteca C mínima e independente para o HAOS
#
# Produz duas coisas:
#   build/crt0.o  — objeto do ponto de entrada, linkado
#                   SEPARADAMENTE por cada programa (não entra em
#                   libc.a: precisa ser sempre o primeiro objeto na
#                   linha de link, e arquivos dentro de um .a não têm
#                   ordem garantida perante o linker).
#   build/libc.a  — todo o resto (syscall, unistd, string, stdlib),
#                   para linkar normalmente com -lc -L build.
#
# Uso típico por um programa de usuário (ver userland_examples/):
#   x86_64-linux-gnu-gcc -ffreestanding -fno-pic -mno-red-zone \
#       -mno-sse -mno-sse2 -nostdlib -I libc/include \
#       -c meuprograma.c -o meuprograma.o
#   x86_64-linux-gnu-ld -T libc/hexec.ld -o meuprograma.elf \
#       libc/build/crt0.o meuprograma.o -L libc/build -lc

CROSS  ?= x86_64-linux-gnu
CC      = $(CROSS)-gcc
AR      = $(CROSS)-ar
NASM    = nasm

BUILD_DIR = build

CFLAGS = \
    -ffreestanding -fno-stack-protector -fno-pic \
    -mno-red-zone -mno-mmx -mno-sse -mno-sse2    \
    -nostdlib -nodefaultlibs                     \
    -O2 -Wall -Wextra                            \
    -std=c11                                     \
    -I include                                   \
    -m64

NASMFLAGS = -f elf64

# ---- Fontes ----
CRT_ASM_SRC = src/crt/crt0.asm
SYSCALL_ASM_SRC = src/syscall/syscall.asm

C_SRCS = src/unistd/write.c   \
         src/unistd/exit.c    \
         src/stdlib/exit.c    \
         src/string/memcpy.c  \
         src/string/memset.c  \
         src/string/memmove.c \
         src/string/memcmp.c  \
         src/string/strlen.c  \
         src/string/strcpy.c  \
         src/string/strncpy.c \
         src/string/strcat.c  \
         src/string/strcmp.c  \
         src/string/strncmp.c

CRT_OBJ     = $(BUILD_DIR)/crt0.o
SYSCALL_OBJ = $(BUILD_DIR)/syscall.o
C_OBJS      = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))

LIBC_A = $(BUILD_DIR)/libc.a

.PHONY: all clean

all: $(CRT_OBJ) $(LIBC_A)

$(BUILD_DIR)/crt0.o: $(CRT_ASM_SRC)
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: $(SYSCALL_ASM_SRC)
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_A): $(SYSCALL_OBJ) $(C_OBJS)
	$(AR) rcs $@ $^
	@echo "  [OK] $(LIBC_A) gerada"

clean:
	rm -rf $(BUILD_DIR)
	@echo "  [OK] libc limpa"
