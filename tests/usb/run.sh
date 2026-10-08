#!/usr/bin/env bash
# Teste de host da tradução HID -> scancodes. Compila só usb_hid.c + o teste.
set -euo pipefail
cd "$(dirname "$0")/../.."
gcc -O1 -g -Wall -Wextra -std=gnu11 -o /tmp/haos_usb_test \
    tests/usb/test_usb_hid.c drivers/usb_hid.c
/tmp/haos_usb_test
