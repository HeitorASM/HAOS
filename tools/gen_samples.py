#!/usr/bin/env python3
"""Gera as imagens de exemplo do HAOS e os valores esperados dos testes.

    python3 tools/gen_samples.py

Escreve:
  kernel/generated/samples_data.h   -- arrays C (instalados em /share/images pelo vfs.c)
  tests/imgview/expected.h          -- tamanho + pixels esperados (ARGB) de cada exemplo

Os PNGs usam, de propósito, TODOS os filtros (0..4, em rodízio), vários chunks IDAT
e tipos de cor diferentes, para exercitar o decodificador de examples/imgview/imgdec.h.
Só usa a biblioteca padrão do Python.
"""
import math, struct, zlib, os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc: return a
    if pb <= pc: return b
    return c

def png(w, h, ctype, depth, rows, plte=None, trns=None):
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)
    raw = bytearray()
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        ft = y % 5
        out = bytearray(len(row))
        for i, x in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            f = [x, x - a, x - b, x - ((a + b) >> 1), x - paeth(a, b, c)][ft]
            out[i] = f & 255
        raw += bytes([ft]) + out
        prev = row
    z = zlib.compress(bytes(raw), 9)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))
    if plte: data += chunk(b"PLTE", bytes(plte))
    if trns: data += chunk(b"tRNS", bytes(trns))
    for i in range(0, len(z), 700):                      # vários IDAT
        data += chunk(b"IDAT", z[i:i + 700])
    return data + chunk(b"IEND", b"")

def pack_bits(vals, depth):
    out = bytearray()
    cur, nb = 0, 0
    for v in vals:
        cur = (cur << depth) | v
        nb += depth
        if nb == 8:
            out.append(cur); cur, nb = 0, 0
    if nb:
        out.append(cur << (8 - nb))
    return bytes(out)

samples = []   # (nome_arquivo, bytes, w, h, lista de ARGB linha a linha)

# 1) gradiente.png — RGB 8 bits, 160x100
w, h = 160, 100
px, rows = [], []
for y in range(h):
    row = bytearray()
    for x in range(w):
        r, g, b = x * 255 // (w - 1), y * 255 // (h - 1), (x + y) * 255 // (w + h - 2)
        row += bytes([r, g, b]); px.append(0xFF000000 | r << 16 | g << 8 | b)
    rows.append(bytes(row))
samples.append(("gradiente.png", png(w, h, 2, 8, rows), w, h, px))

# 2) circulo.png — RGBA 8 bits, 128x128 (borda suave, alfa variável)
w, h = 128, 128
px, rows = [], []
for y in range(h):
    row = bytearray()
    for x in range(w):
        d = math.hypot(x - 63.5, y - 63.5)
        a = max(0, min(255, int((58 - d) * 32)))
        r, g, b = 255 - x * 2, 80 + y, 40 + x
        row += bytes([r, g, b, a]); px.append(a << 24 | r << 16 | g << 8 | b)
    rows.append(bytes(row))
samples.append(("circulo.png", png(w, h, 6, 8, rows), w, h, px))

# 3) paleta.png — 4 bits com paleta (16 cores) e tRNS no índice 0, 64x64
w, h = 64, 64
palette = [(i * 16, 255 - i * 16, (i * 37) % 256) for i in range(16)]
plte = [c for rgb in palette for c in rgb]
trns = [0] + [255] * 15
px, rows = [], []
for y in range(h):
    idx = [((x // 4) + (y // 4)) % 16 for x in range(w)]
    rows.append(pack_bits(idx, 4))
    for i in idx:
        r, g, b = palette[i]
        px.append((0 if i == 0 else 255) << 24 | r << 16 | g << 8 | b)
samples.append(("paleta.png", png(w, h, 3, 4, rows, plte, trns), w, h, px))

# 4) cinza.png — tons de cinza 8 bits, 64x48
w, h = 64, 48
px, rows = [], []
for y in range(h):
    row = bytearray()
    for x in range(w):
        v = (x * 4 + y * 3) & 255
        row.append(v); px.append(0xFF000000 | v << 16 | v << 8 | v)
    rows.append(bytes(row))
samples.append(("cinza.png", png(w, h, 0, 8, rows), w, h, px))

# 5) tabuleiro.bmp — 24 bits, de baixo para cima, 96x72 (largura*3 não múltipla de 4 -> padding)
w, h = 96, 72
px = []
rowdata = []
for y in range(h):
    row = bytearray()
    for x in range(w):
        if y < 8:   r, g, b = (x * 255 // (w - 1), 255 - x * 255 // (w - 1), 128)
        elif ((x // 8) + (y // 8)) % 2: r, g, b = (230, 230, 230)
        else:       r, g, b = (30, 60, 140)
        row += bytes([b, g, r]); px.append(0xFF000000 | r << 16 | g << 8 | b)
    row += bytes((-len(row)) % 4)
    rowdata.append(bytes(row))
pix = b"".join(reversed(rowdata))                     # BMP clássico: última linha primeiro
bmp = b"BM" + struct.pack("<IHHI", 54 + len(pix), 0, 0, 54)
bmp += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(pix), 2835, 2835, 0, 0) + pix
samples.append(("tabuleiro.bmp", bmp, w, h, px))

def cname(fname):
    return fname.replace(".", "_").replace("-", "_")

# ---- header de dados ----
with open(os.path.join(ROOT, "kernel/generated/samples_data.h"), "w") as f:
    f.write("// GERADO por tools/gen_samples.py — não edite à mão.\n#pragma once\n\n")
    for name, data, *_ in samples:
        c = cname(name)
        f.write(f"#define SAMPLE_{c.upper()}_SIZE {len(data)}\n")
        f.write(f"static const unsigned char sample_{c}[SAMPLE_{c.upper()}_SIZE] = {{\n")
        for i in range(0, len(data), 16):
            f.write("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
        f.write("};\n\n")
    f.write("typedef struct { const char* name; const unsigned char* data; unsigned size; } SampleFile;\n")
    f.write("static const SampleFile g_sample_files[] = {\n")
    for name, data, *_ in samples:
        c = cname(name)
        f.write(f'    {{ "{name}", sample_{c}, SAMPLE_{c.upper()}_SIZE }},\n')
    f.write("};\n")

# ---- valores esperados dos testes ----
with open(os.path.join(ROOT, "tests/imgview/expected.h"), "w") as f:
    f.write("// GERADO por tools/gen_samples.py — não edite à mão.\n#pragma once\n\n")
    f.write("typedef struct { const char* name; const unsigned char* data; unsigned size;\n")
    f.write("                 int w, h; unsigned long long sum; unsigned probe[5]; } ExpectedImg;\n")
    f.write("static const ExpectedImg g_expected[] = {\n")
    for name, data, w, h, px in samples:
        c = cname(name)
        s = sum(px)
        probes = [px[0], px[w - 1], px[(h // 2) * w + w // 2], px[(h - 1) * w], px[-1]]
        f.write(f'    {{ "{name}", sample_{c}, SAMPLE_{c.upper()}_SIZE, {w}, {h}, {s}ULL, '
                f'{{ 0x{probes[0]:08x}u, 0x{probes[1]:08x}u, 0x{probes[2]:08x}u, 0x{probes[3]:08x}u, 0x{probes[4]:08x}u }} }},\n')
    f.write("};\n")

for name, data, w, h, px in samples:
    print(f"{name:16s} {len(data):6d} bytes  {w}x{h}")
