#!/usr/bin/env bash
# Teste de host do decodificador PNG/BMP — compila só o teste (NÃO o kernel).
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=gnu11 -fsanitize=address,undefined -o /tmp/haos_imgdec_test \
    tests/imgview/test_imgdec.c
/tmp/haos_imgdec_test
