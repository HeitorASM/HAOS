#!/usr/bin/env python3
"""
mkhexec.py — empacota um ELF linkado contra libc/hexec.ld num
arquivo .hexec v2, com .text/.rodata/.data/.bss como seções
separadas (permissões RX/RO/RW+NX independentes — ver
kernel/hexec.h). Extrai tudo automaticamente via `nm`/`objcopy
--dump-section`, sem exigir nenhum valor manual.

Uso: python3 mkhexec.py <entrada.elf> <saida.hexec> [--cross PREFIXO] [--v1]

--v1 gera o formato antigo (bloco único RW+USER, sem separação de
seções) — mantido só para compatibilidade/comparação; todo programa
novo linkado contra libc/hexec.ld já produz seções alinhadas a
página, então não há motivo para preferir --v1 exceto depuração.

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

HEXEC_MAGIC      = 0x43455848  # "HXEC"
HEXEC_VERSION_1  = 1
HEXEC_VERSION_2  = 2
HEXEC_LOAD_ADDR  = 0x300000000  # ver kernel/hexec.h
PAGE_SIZE        = 4096


def run(cmd, allow_fail=False):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 and not allow_fail:
        sys.stderr.write(f"Comando falhou: {' '.join(cmd)}\n")
        sys.stderr.write(result.stderr)
        sys.exit(1)
    return result.stdout


def get_symbol(elf_path, cross, name):
    """Lê o endereço de um símbolo via `nm`. Retorna None se ausente
    (acontece quando uma seção fica vazia — ex.: um programa sem
    nenhuma variável .data — e o linker script não gera símbolo de
    tamanho zero)."""
    out = run([f"{cross}-nm", elf_path])
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
    return None


def get_entry_point(elf_path, cross):
    out = run([f"{cross}-readelf", "-h", elf_path])
    for line in out.splitlines():
        if "Entry point address" in line:
            return int(line.split()[-1], 16)
    sys.stderr.write("Não consegui encontrar o entry point no ELF.\n")
    sys.exit(1)


def dump_section(elf_path, cross, section_name):
    """Extrai os bytes brutos de uma seção ELF. Retorna b'' se a
    seção não existir ou estiver vazia (ex.: um programa sem
    .rodata) — não é erro, só significa "seção ausente/vazia"."""
    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp_path = tmp.name
    try:
        run([f"{cross}-objcopy", f"--dump-section={section_name}={tmp_path}", elf_path],
            allow_fail=True)
        if not os.path.exists(tmp_path) or os.path.getsize(tmp_path) == 0:
            return b""
        with open(tmp_path, "rb") as f:
            return f.read()
    finally:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)


def pad_to_page(data):
    """Completa `data` com zeros até o próximo múltiplo de PAGE_SIZE
    — o loader do kernel exige cada seção alinhada a página no
    arquivo (ver comentário em kernel/hexec.h e libc/hexec.ld)."""
    remainder = len(data) % PAGE_SIZE
    if remainder == 0:
        return data
    return data + b"\x00" * (PAGE_SIZE - remainder)


def get_bss_size(elf_path, cross):
    start = get_symbol(elf_path, cross, "__hexec_bss_start")
    end = get_symbol(elf_path, cross, "__hexec_bss_end")
    if start is None or end is None:
        return 0
    return end - start


def make_v2(elf_path, out_path, cross):
    entry_point = get_entry_point(elf_path, cross)
    entry_offset = entry_point - HEXEC_LOAD_ADDR

    text_data   = pad_to_page(dump_section(elf_path, cross, ".text"))
    rodata_data = pad_to_page(dump_section(elf_path, cross, ".rodata"))
    data_data   = pad_to_page(dump_section(elf_path, cross, ".data"))
    bss_size    = get_bss_size(elf_path, cross)

    if len(text_data) == 0:
        sys.stderr.write("Erro: .text vazio ou ausente — o ELF foi linkado com libc/hexec.ld?\n")
        sys.exit(1)

    header = struct.pack(
        "<IIQQQQQ",
        HEXEC_MAGIC,
        HEXEC_VERSION_2,
        entry_offset,
        len(text_data),
        len(rodata_data),
        len(data_data),
        bss_size,
    )

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(text_data)
        f.write(rodata_data)
        f.write(data_data)

    print(f"OK: {out_path} (HEXEC v2)")
    print(f"  entry_offset={entry_offset}")
    print(f"  text={len(text_data)}  rodata={len(rodata_data)}  "
          f"data={len(data_data)}  bss={bss_size}")


def make_v1(elf_path, out_path, cross):
    """Formato antigo (v1): .text+.rodata+.data concatenados como um
    único bloco RW+USER, sem alinhamento de página entre eles. Só
    para depuração/comparação — ver docstring do módulo."""
    entry_point = get_entry_point(elf_path, cross)
    entry_offset = entry_point - HEXEC_LOAD_ADDR

    with tempfile.NamedTemporaryFile(delete=False) as tmp:
        tmp_bin_path = tmp.name
    try:
        run([f"{cross}-objcopy", "-O", "binary", elf_path, tmp_bin_path])
        with open(tmp_bin_path, "rb") as f:
            code = f.read()
    finally:
        os.unlink(tmp_bin_path)

    bss_size = get_bss_size(elf_path, cross)

    header = struct.pack("<IIQQQ", HEXEC_MAGIC, HEXEC_VERSION_1,
                          entry_offset, len(code), bss_size)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(code)

    print(f"OK: {out_path} (HEXEC v1)")
    print(f"  entry_offset={entry_offset}  code_size={len(code)}  bss_size={bss_size}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    elf_path = sys.argv[1]
    out_path = sys.argv[2]
    cross = "x86_64-linux-gnu"
    if "--cross" in sys.argv:
        cross = sys.argv[sys.argv.index("--cross") + 1]

    if "--v1" in sys.argv:
        make_v1(elf_path, out_path, cross)
    else:
        make_v2(elf_path, out_path, cross)


if __name__ == "__main__":
    main()
