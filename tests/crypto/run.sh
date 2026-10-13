#!/usr/bin/env bash
# Teste de host do módulo de criptografia — compila SÓ sha256.c + o teste
# (NÃO compila o kernel). Leva menos de 1 segundo.
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O2 -Wall -Wextra -std=c11 -o /tmp/haos_crypto_test \
    tests/crypto/test_crypto.c kernel/sha256.c
/tmp/haos_crypto_test
