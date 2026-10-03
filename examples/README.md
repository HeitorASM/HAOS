# examples/ — programas de exemplo escritos contra a libc do HAOS

Substitui a antiga pasta `userland/` (removida). Cada subpasta aqui
é um programa HEXEC de exemplo, escrito em C usando `libc/`, servindo
tanto de prova de conceito quanto de referência para você escrever
os seus próprios.

## hello_c/

O mais simples possível: escreve uma linha via `write()` e sai com
um código de saída de teste (7), usando só `unistd.h`. É o exemplo
embutido por padrão no VFS do HAOS (ver
`kernel/generated/hello_hexec_data.h` e `fs/vfs.c`) — todo boot já
tem `/bin/hello.hexec` disponível para testar com `run hello.hexec`.

## systest/ — autoteste das syscalls

Roda ~50 verificações e imprime `[PASS]`/`[FAIL]` para cada uma:
`getpid`, `yield`, `uptime_ms`, `sleep_ms`, `stat`, `readdir`,
`getcwd`, `chdir`, `mkdir`, `unlink`, `lseek`, caminhos relativos,
fd de arquivo removido e as correções de segurança do kernel (nome de
arquivo longo, ponteiro de kernel que "dá a volta", buffer somente
leitura). **Rode depois de qualquer mudança em syscalls**:
`run systest.hexec` — o resultado esperado termina com `TUDO OK`.

## winpaint/ — janela gráfica (SYS_WIN_*)

Mini editor de desenho em uma janela própria, criada por um programa
Ring 3: `run winpaint.hexec`. Arraste com o botão esquerdo para desenhar,
clique numa cor (ou tecle `1`..`8`) para trocar, `c` limpa, `q` sai.
Mostra o ciclo completo: `win_create`, desenho (`win_fill_rect`,
`win_text_bg`), laço de eventos (`win_event`) e como reagir a mouse,
teclado, redimensionamento e ao fechamento da janela pelo X.

**Não vem pré-compilado**: gere com `bash tools/build_apps.sh` (compila a
libc, o systest, o ls e o winpaint, e cria os headers em
`kernel/generated/`) e depois `make` no kernel — o `winpaint.hexec` entra
em `/bin` no boot. (Sem o header gerado, o kernel simplesmente compila
sem ele.)

## ls/ — lista diretórios

`run ls.hexec` (diretório atual), `run ls.hexec /bin`,
`run ls.hexec notes.txt`. Exemplo de uso de `readdir()`/`stat()`.

Ambos já vêm embutidos em `/bin` (tabela `g_builtin_bins` em
`fs/vfs.c`) — e **são sincronizados no boot**, inclusive em discos
já formatados: um executável embutido novo ou alterado substitui o
de `/bin` automaticamente (arquivos idênticos não são tocados).

### Como recompilar qualquer exemplo (ex.: systest)

```bash
cd libc && make && cd ..
CF="-ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 -mcmodel=large -nostdlib \
    -nodefaultlibs -O2 -Wall -Wextra -std=c11 -I libc/include -m64"
x86_64-linux-gnu-gcc $CF -c examples/systest/main.c -o /tmp/systest.o
x86_64-linux-gnu-ld -T libc/hexec.ld -o /tmp/systest.elf \
    libc/build/crt0.o /tmp/systest.o -L libc/build -lc
python3 libc/mkhexec.py /tmp/systest.elf examples/systest/systest.hexec
python3 tools/embed_hexec.py examples/systest/systest.hexec \
    kernel/generated/systest_hexec_data.h SYSTEST_HEXEC systest_hexec_data
```

Note o `-mcmodel=large`: é obrigatório (o endereço de carga fica
acima de 4 GB). Depois, `make` no kernel.

### Como recompilar este exemplo

```bash
cd examples/hello_c
x86_64-linux-gnu-gcc -ffreestanding -fno-stack-protector -fno-pic \
    -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
    -nostdlib -nodefaultlibs -O2 -Wall -Wextra -std=c11 \
    -I ../../libc/include -m64 \
    -c main.c -o main.o

x86_64-linux-gnu-ld -T ../../libc/hexec.ld -o hello_c.elf \
    ../../libc/build/crt0.o main.o \
    -L ../../libc/build -lc

python3 ../../libc/mkhexec.py hello_c.elf hello_c.hexec
```

(Requer `libc/build/libc.a` e `libc/build/crt0.o` já compilados —
ver `libc/README.md`.)

### Para atualizar o exemplo embutido no VFS padrão

Depois de gerar um novo `hello_c.hexec`, regenere o header C
embutido e copie para dentro do kernel:

```bash
python3 - << 'EOF'
data = open("hello_c.hexec", "rb").read()
with open("hello_hexec_data.h", "w") as f:
    f.write("#pragma once\n#include \"types.h\"\n\n")
    f.write(f"#define HELLO_HEXEC_SIZE {len(data)}\n")
    f.write("static const unsigned char hello_hexec_data[HELLO_HEXEC_SIZE] = {\n")
    for i in range(0, len(data), 16):
        f.write("    " + ", ".join(f"0x{b:02x}" for b in data[i:i+16]) + ",\n")
    f.write("};\n")
EOF
cp hello_hexec_data.h ../../kernel/generated/hello_hexec_data.h
```

Depois `make clean && make all` no kernel para reincorporar.
