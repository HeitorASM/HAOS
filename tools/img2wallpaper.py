#!/usr/bin/env python3
"""
img2wallpaper.py — HAOS Wallpaper Converter
Converte uma imagem (PNG, JPG, BMP, etc.) para um array C 32bpp (0x00RRGGBB)
pronto para ser usado como wallpaper no HAOS.

Uso:
    python3 tools/img2wallpaper.py assets/minha_imagem.png
    python3 tools/img2wallpaper.py assets/minha_imagem.png --name meu_wallpaper
    python3 tools/img2wallpaper.py assets/minha_imagem.png --width 1024 --height 768
    python3 tools/img2wallpaper.py assets/   # converte todas as imagens da pasta

Saída:
    assets/wallpapers/wallpaper_<nome>.c   — array C com os pixels
    assets/wallpapers/wallpaper_<nome>.h   — header com declaração extern
    assets/wallpapers/wallpapers.h         — header mestre (inclui todos)
"""

import sys
import os
import re
import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("[ERRO] Pillow não encontrado. Instale com:  pip install Pillow")
    sys.exit(1)


# ── Resolução padrão do HAOS (VESA 1024×768) ──────────────────────────────
DEFAULT_WIDTH  = 1024
DEFAULT_HEIGHT = 768

# ── Extensões de imagem suportadas ────────────────────────────────────────
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".tiff", ".webp"}


def sanitize_name(name: str) -> str:
    """Transforma o nome do arquivo em identificador C válido."""
    name = re.sub(r"[^a-zA-Z0-9_]", "_", name)
    if name[0].isdigit():
        name = "_" + name
    return name.lower()


def resize_image(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """Redimensiona a imagem preenchendo (cover) ou fit, conforme o tamanho."""
    src_w, src_h = img.size

    # Se já está no tamanho certo, não faz nada
    if src_w == target_w and src_h == target_h:
        return img

    # Escala "cover": preenche o canvas sem distorcer (crop central)
    scale = max(target_w / src_w, target_h / src_h)
    new_w = int(src_w * scale)
    new_h = int(src_h * scale)
    img = img.resize((new_w, new_h), Image.LANCZOS)

    # Crop central
    left = (new_w - target_w) // 2
    top  = (new_h - target_h) // 2
    img = img.crop((left, top, left + target_w, top + target_h))
    return img


def image_to_c_array(img_path: Path, output_dir: Path,
                     var_name: str | None,
                     target_w: int, target_h: int) -> str:
    """
    Converte a imagem para .c + .h e retorna o nome da variável gerada.
    """
    img = Image.open(img_path).convert("RGB")
    img = resize_image(img, target_w, target_h)

    base = var_name or sanitize_name(img_path.stem)
    c_var  = f"wallpaper_{base}"
    c_file = output_dir / f"wallpaper_{base}.c"
    h_file = output_dir / f"wallpaper_{base}.h"

    total  = target_w * target_h
    pixels = img.load()

    # ── Gera o .c ─────────────────────────────────────────────────────────
    with open(c_file, "w") as f:
        f.write(f"// wallpaper_{base}.c — gerado automaticamente por img2wallpaper.py\n")
        f.write(f"// Fonte: {img_path.name}  ({target_w}×{target_h} px)\n")
        f.write(f"// NÃO edite manualmente. Execute: python3 tools/img2wallpaper.py\n\n")
        f.write(f'#include "wallpaper_{base}.h"\n\n')
        f.write(f"// {total} pixels, formato 0x00RRGGBB, 32bpp\n")
        f.write(f"const unsigned int {c_var}[{total}] = {{\n")

        cols_per_line = 8
        for y in range(target_h):
            f.write("    ")
            for x in range(target_w):
                r, g, b = pixels[x, y]
                color = (r << 16) | (g << 8) | b
                f.write(f"0x{color:06X}")
                if not (y == target_h - 1 and x == target_w - 1):
                    f.write(",")
                if (y * target_w + x + 1) % cols_per_line == 0:
                    f.write("\n    ")
            # garante quebra de linha ao final de cada linha da imagem
            if (y * target_w + target_w) % cols_per_line != 0:
                f.write("\n")

        f.write("};\n\n")
        f.write(f"const unsigned int {c_var}_width  = {target_w};\n")
        f.write(f"const unsigned int {c_var}_height = {target_h};\n")

    # ── Gera o .h ─────────────────────────────────────────────────────────
    guard = f"WALLPAPER_{base.upper()}_H"
    with open(h_file, "w") as f:
        f.write(f"// wallpaper_{base}.h — gerado automaticamente por img2wallpaper.py\n")
        f.write(f"#pragma once\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(f"// {target_w}×{target_h} px, 32bpp (0x00RRGGBB)\n")
        f.write(f"extern const unsigned int {c_var}[{total}];\n")
        f.write(f"extern const unsigned int {c_var}_width;\n")
        f.write(f"extern const unsigned int {c_var}_height;\n\n")
        f.write(f"#endif // {guard}\n")

    print(f"  [OK] {c_var}  →  {c_file.name} + {h_file.name}")
    return base


def update_master_header(output_dir: Path, all_names: list[str],
                         target_w: int, target_h: int):
    """
    Gera/atualiza assets/wallpapers/wallpapers.h com todos os wallpapers.
    """
    master = output_dir / "wallpapers.h"
    with open(master, "w") as f:
        f.write("// wallpapers.h — gerado automaticamente por img2wallpaper.py\n")
        f.write("// Inclua este arquivo em gui/screens/desktop.c ou config.c\n")
        f.write("#pragma once\n\n")

        for name in all_names:
            f.write(f'#include "wallpaper_{name}.h"\n')

        f.write(f"\n// Número total de wallpapers disponíveis\n")
        f.write(f"#define WALLPAPER_COUNT {len(all_names)}\n\n")
        f.write(f"// Resolução para a qual os wallpapers foram convertidos\n")
        f.write(f"#define WALLPAPER_W {target_w}\n")
        f.write(f"#define WALLPAPER_H {target_h}\n\n")

        # Tabela de ponteiros para fácil indexação por índice numérico
        f.write("// Tabela de ponteiros: wallpaper_table[i] dá acesso ao wallpaper i\n")
        f.write("// Use extern + cast para incluir em translation units sem redefinição:\n")
        f.write("//   #include \"assets/wallpapers/wallpapers.h\"\n")
        f.write("//   const unsigned int *wp = wallpaper_table[config_wallpaper_index];\n\n")
        f.write("#ifdef WALLPAPER_TABLE_IMPL\n")
        f.write("const unsigned int *wallpaper_table[WALLPAPER_COUNT] = {\n")
        for name in all_names:
            f.write(f"    wallpaper_{name},\n")
        f.write("};\n")
        f.write("#else\n")
        f.write("extern const unsigned int *wallpaper_table[WALLPAPER_COUNT];\n")
        f.write("#endif\n\n")

        # Enum para usar no config
        f.write("// Enum para usar no config_get_wallpaper() / config_set_wallpaper()\n")
        f.write("typedef enum {\n")
        for i, name in enumerate(all_names):
            f.write(f"    WALLPAPER_{name.upper()} = {i},\n")
        f.write(f"    WALLPAPER_NONE = {len(all_names)},  // cor sólida / gradiente padrão\n")
        f.write("} WallpaperID;\n")

    print(f"\n  [OK] Header mestre → {master}")


def main():
    parser = argparse.ArgumentParser(
        description="Converte imagens em arrays C para wallpapers do HAOS."
    )
    parser.add_argument(
        "input",
        help="Caminho para a imagem (ou pasta com imagens)"
    )
    parser.add_argument(
        "--name", "-n",
        default=None,
        help="Nome da variável C (padrão: nome do arquivo)"
    )
    parser.add_argument(
        "--width", "-W",
        type=int,
        default=DEFAULT_WIDTH,
        help=f"Largura alvo em pixels (padrão: {DEFAULT_WIDTH})"
    )
    parser.add_argument(
        "--height", "-H",
        type=int,
        default=DEFAULT_HEIGHT,
        help=f"Altura alvo em pixels (padrão: {DEFAULT_HEIGHT})"
    )
    parser.add_argument(
        "--output", "-o",
        default=None,
        help="Diretório de saída (padrão: assets/wallpapers/)"
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_dir = Path(args.output) if args.output else Path("assets/wallpapers")
    output_dir.mkdir(parents=True, exist_ok=True)

    target_w = args.width
    target_h = args.height

    # Coleta imagens a converter
    if input_path.is_dir():
        images = sorted(p for p in input_path.iterdir()
                        if p.suffix.lower() in IMAGE_EXTS)
        if not images:
            print(f"[AVISO] Nenhuma imagem encontrada em {input_path}")
            sys.exit(0)
        print(f"Encontradas {len(images)} imagem(ns) em {input_path}")
    elif input_path.is_file():
        if input_path.suffix.lower() not in IMAGE_EXTS:
            print(f"[ERRO] Extensão não suportada: {input_path.suffix}")
            print(f"       Suportadas: {', '.join(IMAGE_EXTS)}")
            sys.exit(1)
        images = [input_path]
    else:
        print(f"[ERRO] Arquivo ou diretório não encontrado: {input_path}")
        sys.exit(1)

    print(f"Resolução alvo: {target_w}×{target_h}  →  {output_dir}/\n")

    all_names = []
    for img_path in images:
        try:
            name = image_to_c_array(img_path, output_dir,
                                    args.name if len(images) == 1 else None,
                                    target_w, target_h)
            all_names.append(name)
        except Exception as e:
            print(f"  [ERRO] {img_path.name}: {e}")

    if all_names:
        update_master_header(output_dir, all_names, target_w, target_h)

    print(f"\n  {len(all_names)} wallpaper(s) convertido(s) com sucesso.")
    print(f"  Adicione as fontes ao Makefile:")
    for name in all_names:
        print(f"    assets/wallpapers/wallpaper_{name}.c \\")
    print()
    print("  No desktop.c / config.c:")
    print('    #include "assets/wallpapers/wallpapers.h"')
    print("    // (uma vez, em um .c): #define WALLPAPER_TABLE_IMPL")


if __name__ == "__main__":
    main()