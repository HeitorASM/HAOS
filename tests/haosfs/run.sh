#!/usr/bin/env bash
# Teste de queda de energia do HAOSFS (host). Compila vfs.c, haosfs.c, perm.c,
# user.c, sha256.c e random.c reais + os stubs — NÃO compila o kernel.
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=c11 -Wno-unused-parameter -Wno-unused-function \
    -DREAL_HAOSFS -Ikernel -Ifs -Idrivers \
    -o /tmp/haos_journal_test \
    tests/haosfs/test_journal.c tests/perm/stubs.c \
    fs/vfs.c fs/haosfs.c fs/perm.c kernel/user.c kernel/sha256.c kernel/random.c
/tmp/haos_journal_test
