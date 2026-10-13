#!/usr/bin/env bash
# Teste de comportamento do gerenciador de janelas (gui/core/wm.cpp) rodando
# NO HOST — não precisa de QEMU/VirtualBox nem do kernel. Compila o núcleo
# REAL do WM (wm, window, container, widget, layout, theme) com stubs mínimos
# (fb_width/fb_height, kstrncpy, timer_ticks) definidos em wm_test.cpp.
#
#   bash tests/wm/run.sh        (a partir da raiz do projeto)
#
# Cobre: duplo-clique na barra de título, soltar janela maximizada só após
# arrastar de verdade, snap esquerdo/direito/topo (e o tamanho "normal"
# restaurado), Alt+Tab (wm_focus_next), wm_window_at e wm_unminimize.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
FL="-I. -Ikernel -Idrivers -Igui -Ifs -Igui/core -Igui/elements -std=c++17 -fno-exceptions -fno-rtti -O0 -w"
for f in wm window container widget layout theme; do g++ $FL -c gui/core/$f.cpp -o "$OUT/$f.o"; done
g++ $FL -c tests/wm/wm_test.cpp -o "$OUT/test.o"
# O teste não desenha nada: símbolos de desenho/VFS nunca são chamados, então
# podem ficar sem definição (ignorados no link).
g++ -o "$OUT/wmtest" "$OUT"/*.o -Wl,--unresolved-symbols=ignore-all
"$OUT/wmtest"
