# ============================================================
#  HAOS v1.1 
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
echo -e "${BLUE}║     HAOS v1.1  —  Build & Run    ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════╝${NC}"
echo ""

# ---- Pergunta se quer executar após build --------------------
echo -e "${YELLOW}O que deseja fazer?${NC}"
echo ""
echo "  1) Apenas compilar (gerar ISO)"
echo "  2) Compilar e executar a máquina"
echo "  3) Executar a máquina (sem compilar novamente)"
echo ""
read -p "Escolha [1-3]: " choice
echo ""

case $choice in
    1)
        info "Apenas compilando HAOS..."
        make clean -s 2>/dev/null || true
        make iso
        ok "Build concluído: haos.iso"
        exit 0
        ;;
    2)
        info "Compilando HAOS..."
        make clean -s 2>/dev/null || true
        make iso
        ok "Build concluído: haos.iso"
        echo ""
        RUN_MODE="run"
        ;;
    3)
        if [ ! -f "haos.iso" ]; then
            error "Arquivo haos.iso não encontrado. Execute a opção 1 ou 2 primeiro."
        fi
        info "Usando haos.iso existente"
        RUN_MODE="run"
        ;;
    *)
        error "Opção inválida"
        ;;
esac

# ---- Pergunta qual modo de execução --------------------------
if [ "$RUN_MODE" = "run" ]; then
    echo ""
    echo -e "${YELLOW}Escolha o modo de exibição:${NC}"
    echo ""
    echo "  1) GTK (recomendado para WSLg)"
    echo "  2) SDL"
    echo "  3) VNC (headless)"
    echo "  4) Auto-detectar (tenta GTK → SDL → VNC)"
    echo ""
    read -p "Escolha [1-4]: " display_choice
    echo ""

    # ---- Flags do QEMU com fix do bug PipeWire -------------------
    QEMU="qemu-system-x86_64"
    QEMU_BASE="-cdrom haos.iso -boot d -m 256M -vga std -no-reboot \
               -audiodev none,id=noaudio \
               -machine pc,accel=tcg \
               -device ps2-mouse -rtc base=localtime"

    info "Atalhos no HAOS:"
    echo "    [Enter]  → Tela de boas-vindas"
    echo "    [S]      → Menu Iniciar"
    echo "    [T]      → Terminal"
    echo "    [A]      → Sobre"
    echo "    [ESC]    → Fechar menu"
    echo ""

    case $display_choice in
        1)
            info "Iniciando com GTK..."
            $QEMU $QEMU_BASE -display gtk
            ;;
        2)
            info "Iniciando com SDL..."
            $QEMU $QEMU_BASE -display sdl,gl=off
            ;;
        3)
            info "Iniciando em modo VNC..."
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
            $QEMU $QEMU_BASE -display none -vnc :0
            ;;
        4)
            # Auto-detectar
            try_display() {
                local flag="$1" label="$2"
                info "Tentando: $label"
                if $QEMU $QEMU_BASE $flag > /tmp/qemu_out 2>&1; then
                    return 0
                fi
                local err
                err=$(grep "ERROR\|assert\|Bail\|error" /tmp/qemu_out | head -2)
                warn "Falhou ($label): $err"
                return 1
            }

            if try_display "-display gtk" "GTK (janela nativa)"; then
                exit 0
            fi

            if try_display "-display sdl,gl=off" "SDL"; then
                exit 0
            fi

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
            $QEMU $QEMU_BASE -display none -vnc :0
            ;;
        *)
            error "Opção inválida"
            ;;
    esac
fi