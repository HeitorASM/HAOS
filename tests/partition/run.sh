#!/usr/bin/env bash
# Teste de host do leitor MBR/GPT — compila só partition.c + o teste (NÃO o kernel).
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=gnu11 -o /tmp/haos_part_test \
    tests/partition/test_partition.c drivers/partition.c
/tmp/haos_part_test
