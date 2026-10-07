#!/usr/bin/env bash
# Teste de host de permissões/usuários — compila só vfs.c, perm.c, user.c e
# sha256.c reais + stubs (NÃO compila o kernel inteiro). Poucos segundos.
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=c11 -Wno-unused-parameter -Wno-unused-function \
    -Ikernel -Ifs -Idrivers \
    -o /tmp/haos_perm_test \
    tests/perm/test_perm.c tests/perm/stubs.c \
    fs/vfs.c fs/perm.c kernel/user.c kernel/sha256.c
/tmp/haos_perm_test
