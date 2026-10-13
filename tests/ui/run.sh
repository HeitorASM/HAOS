#!/usr/bin/env bash
# Teste de host do toolkit de widgets — compila ui.c, ui_dialog.c, draw.c, uitheme.c + o teste
# (o kernel é substituído por stubs: tests/ui/test_ui.c). NÃO compila o OS.
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=gnu11 -fsanitize=address,undefined -DHAOS_HOST_BUILD -iquote libc/include \
    -o /tmp/haos_ui_test tests/ui/test_ui.c \
    libc/src/gfx/ui.c libc/src/gfx/ui_dialog.c libc/src/gfx/draw.c libc/src/gfx/uitheme.c
/tmp/haos_ui_test
