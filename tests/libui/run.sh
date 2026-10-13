#!/usr/bin/env bash
# Teste de host do renderizador de apps (cv_*) — compila só draw.c + o teste.
# -iquote: os includes "haos/..." vêm da libc do HAOS, mas <stdlib.h>/<string.h> são do host.
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=gnu11 -fsanitize=address,undefined -DHAOS_HOST_BUILD -iquote libc/include \
    -o /tmp/haos_libui_test tests/libui/test_draw.c libc/src/gfx/draw.c
/tmp/haos_libui_test
