#!/usr/bin/env python3
"""Uso: embed_hexec.py <arquivo.hexec> <saida.h> <PREFIXO_MAIUSCULO> <nome_array>
Gera um header C com os bytes do .hexec embutidos (padrão de kernel/generated/)."""
import sys
src, out, macro, arr = sys.argv[1:5]
data = open(src, "rb").read()
with open(out, "w") as f:
    f.write('#pragma once\n#include "types.h"\n\n')
    f.write(f"#define {macro}_SIZE {len(data)}\n")
    f.write(f"static const unsigned char {arr}[{macro}_SIZE] = {{\n")
    for i in range(0, len(data), 16):
        f.write("    " + ", ".join(f"0x{b:02x}" for b in data[i:i+16]) + ",\n")
    f.write("};\n")
print(f"OK: {out} ({len(data)} bytes)")
