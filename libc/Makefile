# libc/Makefile — biblioteca C mínima e independente para o HAOS
#
# -mcmodel=large é OBRIGATÓRIO: HEXEC_LOAD_ADDR (0x300000000) fica
# acima de 4GB, e o modelo de código padrão do GCC ("small") só gera
# relocations de 32 bits para símbolos globais — qualquer variável
# em .data/.bss falha ao linkar ("relocation truncated to fit") sem
# esta flag. Isso vale tanto para a libc quanto para QUALQUER
# programa de usuário linkado contra libc/hexec.ld — sempre inclua
# -mcmodel=large no CFLAGS de qualquer novo programa (ver
# examples/*/Makefile ou o comando manual documentado em
# libc/README.md).
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
    -mcmodel=large                                \
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
         src/unistd/sbrk.c    \
         src/unistd/open.c    \
         src/unistd/read.c    \
         src/unistd/close.c   \
         src/unistd/write_fd.c \
         src/unistd/spawn.c   \
         src/unistd/waitpid.c \
         src/stdlib/exit.c    \
         src/stdlib/malloc.c  \
         src/stdio/printf.c   \
         src/stdio/scanf.c    \
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
