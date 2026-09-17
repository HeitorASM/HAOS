#!/usr/bin/env python3
"""
mkhexec.py — empacota um ELF linkado contra libc/hexec.ld num
arquivo .hexec, extraindo automaticamente code_size (via objcopy -O
binary) e bss_size (via `size`), em vez de exigir esses valores como
argumentos manuais como na versão anterior deste script (que lidava
com binários "flat" gerados por `nasm -f bin`, sem informação de
seções nenhuma).

Uso: python3 mkhexec.py <entrada.elf> <saida.hexec> [--cross PREFIXO]

O <entrada.elf> deve ter sido linkado com libc/hexec.ld (ENTRY em
_start, carregado em HEXEC_LOAD_ADDR) — ver libc/README.md ou o
Makefile de qualquer projeto em examples/ para o comando de link
completo.
"""
import struct
import subprocess
import sys
import tempfile
import os

HEXEC_MAGIC = 0x43455848  # "HXEC"
HEXEC_VERSION = 1


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"Comando falhou: {' '.join(cmd)}\n")
        sys.stderr.write(result.stderr)
        sys.exit(1)
    return result.stdout


def get_entry_offset(elf_path, cross):
    # O entry point do ELF já é um endereço absoluto (HEXEC_LOAD_ADDR
    # + offset) — subtraímos HEXEC_LOAD_ADDR para obter o offset que
    # o header HEXEC espera. Lido via `readelf -h`, não hardcoded,
    # para que _start não precisar estar sempre no byte 0 (embora na
    # prática, com hexec.ld, sempre esteja — mas não custa ser
    # robusto a essa suposição mudar no futuro).
    out = run([f"{cross}-readelf", "-h", elf_path])
    for line in out.splitlines():
        if "Entry point address" in line:
            addr = int(line.split()[-1], 16)
            HEXEC_LOAD_ADDR = 0x300000000  # ver kernel/hexec.h
            return addr - HEXEC_LOAD_ADDR
    sys.stderr.write("Não consegui encontrar o entry point no ELF.\n")
    sys.exit(1)


def get_bss_size(elf_path, cross):
    out = run([f"{cross}-size", elf_path])
    lines = out.strip().splitlines()
    # Formato padrão do `size`: header, depois "text data bss dec hex filename"
    values = lines[1].split()
    return int(values[2])  # bss é a 3ª coluna


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    elf_path = sys.argv[1]
    out_path = sys.argv[2]
    cross = "x86_64-linux-gnu"
    if "--cross" in sys.argv:
        cross = sys.argv[sys.argv.index("--cross") + 1]

    entry_offset = get_entry_offset(elf_path, cross)
    bss_size = get_bss_size(elf_path, cross)

    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp_bin_path = tmp.name
    try:
        run([f"{cross}-objcopy", "-O", "binary", elf_path, tmp_bin_path])
        with open(tmp_bin_path, "rb") as f:
            code = f.read()
    finally:
        os.unlink(tmp_bin_path)

    header = struct.pack(
        "<IIQQQ",
        HEXEC_MAGIC,
        HEXEC_VERSION,
        entry_offset,
        len(code),
        bss_size,
    )

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(code)

    print(f"OK: {out_path}")
    print(f"  entry_offset={entry_offset}  code_size={len(code)}  bss_size={bss_size}")


if __name__ == "__main__":
    main()
