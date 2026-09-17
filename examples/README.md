# examples/ — programas de exemplo escritos contra a libc do HAOS

Cada subpasta aqui é um programa HEXEC de exemplo/test, escrito em C usando `libc/`, servindo
tanto de prova de conceito quanto de referência para você escrever
os seus próprios.

## hello_c/

O mais simples possível: escreve uma linha via `write()` e sai com
um código de saída de teste (7), usando só `unistd.h`. É o exemplo
embutido por padrão no VFS do HAOS (ver
`kernel/generated/hello_hexec_data.h` e `fs/vfs.c`) — todo boot já
tem `/bin/hello.hexec` disponível para testar com `run hello.hexec`.

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
