#!/usr/bin/env bash
# ============================================================
#  HAOS v1.0 — Script de execução com fix do bug QEMU+PipeWire
# ============================================================
set -e

BLUE='\033[0;34m'; GREEN='\033[0;32m'
YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERRO]${NC}  $*"; exit 1; }

echo ""
echo -e "${BLUE}╔══════════════════════════════════╗${NC}"
echo -e "${BLUE}║     HAOS v1.0  —  Build & Run    ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════╝${NC}"
echo ""

# ---- Instala dependências ------------------------------------
info "Verificando dependências..."
PKGS=()
command -v nasm                  >/dev/null 2>&1 || PKGS+=(nasm)
command -v x86_64-linux-gnu-gcc  >/dev/null 2>&1 || PKGS+=(gcc-x86-64-linux-gnu)
command -v x86_64-linux-gnu-ld   >/dev/null 2>&1 || PKGS+=(binutils-x86-64-linux-gnu)
command -v grub-mkrescue         >/dev/null 2>&1 || PKGS+=(grub-pc-bin grub-common)
command -v xorriso               >/dev/null 2>&1 || PKGS+=(xorriso)
command -v qemu-system-x86_64    >/dev/null 2>&1 || PKGS+=(qemu-system-x86)

if [ ${#PKGS[@]} -gt 0 ]; then
    info "Instalando: ${PKGS[*]}"
    sudo apt-get update -qq
    sudo apt-get install -y -qq "${PKGS[@]}"
    ok "Dependências instaladas"
else
    ok "Todas as dependências já estão presentes"
fi

# ---- Build ---------------------------------------------------
info "Compilando HAOS..."
make clean -s 2>/dev/null || true
make iso
ok "Build concluído: haos.iso"
echo ""

# ---- Flags do QEMU com fix do bug PipeWire -------------------
# CRÍTICO: -audiodev none desabilita o backend de áudio que causa o
# bug qemu_mutex_lock_iothread_impl no QEMU 8.x com WSL2+PipeWire
QEMU="qemu-system-x86_64"
QEMU_COMMON="-cdrom haos.iso -boot d -m 128M -vga std -no-reboot \
             -audiodev none,id=noaudio \
             -machine pc,accel=tcg"

info "Atalhos no HAOS:"
echo "    [Enter]  → Tela de boas-vindas"
echo "    [S]      → Menu Iniciar"
echo "    [T]      → Terminal"
echo "    [A]      → Sobre"
echo "    [ESC]    → Fechar menu"
echo ""

# ---- Tenta displays em ordem de preferência ------------------
try_display() {
    local flag="$1" label="$2"
    info "Tentando: $label"
    if $QEMU $QEMU_COMMON $flag > /tmp/qemu_out 2>&1; then
        return 0
    fi
    local err
    err=$(grep "ERROR\|assert\|Bail\|error" /tmp/qemu_out | head -2)
    warn "Falhou ($label): $err"
    return 1
}

# Tenta GTK (melhor para WSLg)
if try_display "-display gtk" "GTK (janela nativa)"; then
    exit 0
fi

# Tenta SDL sem OpenGL
if try_display "-display sdl,gl=off" "SDL"; then
    exit 0
fi

# VNC headless — sempre funciona
echo ""
warn "GTK e SDL falharam. Iniciando em modo VNC..."
echo ""
echo -e "${YELLOW}  ┌─────────────────────────────────────────────────┐${NC}"
echo -e "${YELLOW}  │  QEMU está rodando em modo VNC headless          │${NC}"
echo -e "${YELLOW}  │                                                   │${NC}"
echo -e "${YELLOW}  │  Para ver a tela do HAOS:                         │${NC}"
echo -e "${YELLOW}  │  1. Baixe TigerVNC Viewer (grátis):               │${NC}"
echo -e "${YELLOW}  │     https://tigervnc.org/                         │${NC}"
echo -e "${YELLOW}  │  2. Abra e conecte em: localhost:5900              │${NC}"
echo -e "${YELLOW}  │  3. Deixe a senha em branco e clique OK            │${NC}"
echo -e "${YELLOW}  │                                                   │${NC}"
echo -e "${YELLOW}  │  Para encerrar: Ctrl+C neste terminal              │${NC}"
echo -e "${YELLOW}  └─────────────────────────────────────────────────┘${NC}"
echo ""

$QEMU $QEMU_COMMON -display none -vnc :0
