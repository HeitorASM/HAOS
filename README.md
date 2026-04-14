# HAOS — Home-built Operating System v1.1

OS bare-metal 64-bit construído do zero em C + ASM.

## Novidades em v1.1


### Recursos
- **Suporte a mouse PS/2** (`drivers/mouse.c`):
  - IRQ12 via ring-buffer no handler assembly
  - Driver processa pacotes de 3 bytes, delta X/Y com extensão de sinal
  - Cursor sprite 12×16 pixels desenhado sobre tudo
  - Janelas arrastáveis pelo título com clique + arrastar
  - Botões de controle (Fechar, Maximizar, Minimizar) clicáveis
  - Menu Iniciar clicável
- **Interface**:
  - Paleta dark mode: `#0B0E1A` fundo, `#5AA0FF` accent, `#00FF99` terminal
  - Janelas com cantos arredondados, botões em círculo (estilo macOS)
  - Sombras com stipple pattern
  - Gradientes de alta qualidade (vertical e horizontal)
  - Ícones detalhados: Terminal (preview de janela), Sobre (letra "i"), Config
  - Taskbar com relógio, botões arredondados e separador luminoso
  - Menu Iniciar com header gradiente e separador
  - Tela de boot com barra de progresso + percentual + glow na ponta
  - Tela de boas-vindas com painel de vidro e lista de recursos
- **Memória expandida**: heap de 4MB → 12MB para comportar os dois buffers extras

## Estrutura

```
OS/
├── boot/         boot.asm — multiboot2 + entrada 64-bit
├── kernel/       kernel, GDT, IDT, PIC, PIT, teclado, memória
├── drivers/      fb.c (framebuffer + double buffer), font.c, mouse.c (novo)
├── gui/          gui.c, window.c, terminal.c
├── iso/          estrutura para grub-mkrescue
├── linker.ld
└── Makefile
```

## Como compilar

```bash
# Dependências (Ubuntu/Debian)
sudo apt install gcc-x86-64-linux-gnu nasm grub-pc-bin xorriso qemu-system-x86

make iso
make run          # tenta GTK → SDL → VNC
make run-gtk      # força GTK (WSLg)
make run-sdl      # força SDL
make run-vnc      # headless, conectar em localhost:5900
```

## Atalhos

| Tecla | Ação |
|-------|------|
| `[S]` | Abrir/fechar Menu Iniciar (sem janela focada) |
| `[T]` | Abrir Terminal (sem janela focada) |
| `[A]` | Abrir "Sobre" (sem janela focada) |
| `[ESC]` | Fechar Menu Iniciar |
| Mouse | Arrastar janela pela barra de título |
| Mouse | Clicar no `x` para fechar janela |

## Comandos do Terminal

```
help      — lista os comandos
clear     — limpa a tela
echo ...  — imprime texto
about     — info do sistema
ls        — lista arquivos (simulado)
date      — data/hora (placeholder)
mem       — info de memória
color     — paleta de cores
reboot    — reinicia via PS/2
```
