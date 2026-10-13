#!/usr/bin/env bash
# Teste de host do decodificador PNG/BMP — compila só o teste (NÃO o kernel).
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tools/gen_samples.py   # gera kernel/generated/samples_data.h e expected.h
gcc -O1 -g -Wall -Wextra -std=gnu11 -fsanitize=address,undefined -o /tmp/haos_imgdec_test \
    tests/imgview/test_imgdec.c
/tmp/haos_imgdec_test
