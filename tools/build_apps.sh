#!/usr/bin/env bash
# Compila a libc e os programas de usuário de examples/ e regenera os
# headers embutidos em kernel/generated/ — um comando só, em vez dos passos
# manuais de libc/README.md.
#
#   bash tools/build_apps.sh                 # winpaint, imgview e uidemo
#   bash tools/build_apps.sh winpaint        # só os nomes listados
#
# Para cada <nome> lê examples/<nome>/main.c e gera:
#   examples/<nome>/<nome>.hexec
#   examples/<nome>/<nome>_hexec_data.h  e  kernel/generated/<nome>_hexec_data.h
# O nome do array/macro segue o padrão <nome>_hexec_data / <NOME>_HEXEC_SIZE.
# Depois rode `make` do kernel. Variáveis: CROSS (padrão x86_64-linux-gnu).
set -euo pipefail
cd "$(dirname "$0")/.."

CROSS="${CROSS:-x86_64-linux-gnu}"
CC="$CROSS-gcc"; LD="$CROSS-ld"
APPS=("$@"); [ ${#APPS[@]} -eq 0 ] && APPS=(winpaint imgview uidemo)

CF="-ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    -mcmodel=large -nostdlib -nodefaultlibs -O2 -Wall -Wextra -std=c11 -I libc/include -m64"

echo "== libc =="
make -C libc >/dev/null

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
for n in "${APPS[@]}"; do
    src="examples/$n/main.c"
    [ -f "$src" ] || { echo "pulando '$n': $src nao existe"; continue; }
    echo "== $n =="
    # shellcheck disable=SC2086
    $CC $CF -c "$src" -o "$TMP/$n.o"
    $LD -T libc/hexec.ld -o "$TMP/$n.elf" libc/build/crt0.o "$TMP/$n.o" -L libc/build -lc
    python3 libc/mkhexec.py "$TMP/$n.elf" "examples/$n/$n.hexec" >/dev/null
    MACRO="$(echo "$n" | tr '[:lower:]' '[:upper:]')_HEXEC"
    python3 tools/embed_hexec.py "examples/$n/$n.hexec" "examples/$n/${n}_hexec_data.h" "$MACRO" "${n}_hexec_data" >/dev/null
    cp "examples/$n/${n}_hexec_data.h" "kernel/generated/${n}_hexec_data.h"
    echo "   ok: $(stat -c%s "examples/$n/$n.hexec") bytes"
done
rm -rf libc/build
echo "[OK!] Build [OK!]"
