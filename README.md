# HAOS — Home-built Operating System v1.1

[![Architecture](https://img.shields.io/badge/Arch-x86__64-blue)](https://en.wikipedia.org/wiki/X86-64)
[![Language](https://img.shields.io/badge/Lang-C%20%2B%20ASM-orange)]()
[![Boot](https://img.shields.io/badge/Boot-Multiboot2%20%2B%20GRUB-green)]()

O HAOS é um sistema operacional bare-metal de 64 bits desenvolvido de forma independente e educacional. O projeto visa a implementação de um núcleo funcional em arquitetura x86_64, utilizando C freestanding e Assembly, sem dependências de bibliotecas externas ou do sistema hospedeiro.

---

![HAOS rodando no VirtualBox](assets/README/VirtualBox_HAOS.png)

> UI do sistema

---

## Arquitetura do Sistema

### Diagrama de Componentes

```mermaid
graph TB
    subgraph Hardware[" Hardware"]
        CPU["CPU x86-64"]
        RAM["Memória RAM"]
        DISK["Disco / BIOS"]
        IO["I/O Ports"]
    end
    
    subgraph Boot[" Fase de Boot"]
        GRUB["GRUB Bootloader"]
        MB2["Multiboot2 Protocol"]
        ASM["Assembly x86-64"]
    end
    
    subgraph Kernel[" Kernel"]
        GDT["GDT - Global Descriptor Table"]
        IDT["IDT - Interrupt Descriptor Table"]
        PIC["PIC - Programmable Interrupt Controller"]
        PIT["PIT - Programmable Interval Timer<br/>100 Hz Clock"]
        KMEM["Gerenciador de Memória<br/>Heap: 12 MB"]
        KBD["Keyboard Handler"]
        RTC["RTC - Real Time Clock"]
    end
    
    subgraph Drivers[" Drivers"]
        FB["Framebuffer Driver<br/>Double Buffering"]
        MOUSE["Mouse PS/2 Driver"]
        FONT["Font Renderer<br/>UTF-8 ↔ CP437"]
    end
    
    subgraph FS[" Virtual File System"]
        VFS["VFS Tree Manager<br/>Max: 32 nodes<br/>Max file: 256 bytes"]
    end
    
    subgraph GUI[" Interface Gráfica"]
        WM["Window Manager"]
        DESKTOP["Desktop"]
        TASKBAR["Taskbar + StartMenu"]
        WALLPAPER["Wallpaper System"]
    end
    
    subgraph Apps[" Aplicativos"]
        TERM["Terminal"]
        ABOUT["About"]
        CONFIG["Settings"]
    end
    
    DISK -->|Boot| GRUB
    GRUB -->|Multiboot2| MB2
    MB2 --> ASM
    ASM -->|Init| GDT
    GDT --> IDT
    IDT -->|Setup| PIC
    PIC --> PIT
    CPU -->|Memory| KMEM
    IO -->|Keyboard| KBD
    IO -->|RTC| RTC
    
    KMEM --> FB
    KMEM --> MOUSE
    KMEM --> FONT
    KMEM --> VFS
    
    KBD --> DESKTOP
    MOUSE --> WM
    FB --> WM
    FONT --> WM
    
    WM --> TASKBAR
    WM --> WALLPAPER
    WM --> DESKTOP
    
    DESKTOP --> TERM
    TASKBAR --> TERM
    TERM --> ABOUT
    TERM --> CONFIG
    
    TERM --> VFS
    CONFIG --> WALLPAPER
```

---

##  Fluxo de Inicialização (Boot)

```mermaid
sequenceDiagram
    participant FIRMWARE as BIOS/UEFI
    participant GRUB as GRUB Bootloader
    participant BOOT as Boot.asm
    participant KERNEL as Kernel
    participant GUI as GUI
    
    FIRMWARE->>GRUB: Localiza GRUB
    activate GRUB
    GRUB->>GRUB: Carrega kernel binário
    GRUB->>GRUB: Configura Multiboot2 tags
    GRUB->>BOOT: Salta para kernel entry point
    deactivate GRUB
    
    activate BOOT
    BOOT->>BOOT: 1. Disable Interrupts (CLI)
    BOOT->>BOOT: 2. Setup Page Tables (Paging)
    BOOT->>BOOT: 3. Enable Long Mode (LA57/EFER)
    BOOT->>BOOT: 4. Jump to 64-bit code
    BOOT->>KERNEL:  Long Mode habilitado
    deactivate BOOT
    
    activate KERNEL
    KERNEL->>KERNEL: Inicializar GDT (64-bit)
    KERNEL->>KERNEL: Carregar GDT (LGDT)
    KERNEL->>KERNEL: Pular para seletor correto
    KERNEL->>KERNEL: Zerar .bss section
    KERNEL->>KERNEL: Configurar stack pointer
    
    KERNEL->>KERNEL: Inicializar IDT
    KERNEL->>KERNEL: Setup 256 interrupt handlers
    KERNEL->>KERNEL: Carregar IDT (LIDT)
    
    KERNEL->>KERNEL: Inicializar Framebuffer
    KERNEL->>KERNEL: Inicializar PIC (8259)
    KERNEL->>KERNEL: Inicializar PIT (100 Hz)
    KERNEL->>KERNEL: Inicializar Memory Manager
    KERNEL->>KERNEL: Inicializar RTC
    
    KERNEL->>KERNEL: Enable Interrupts (STI)
    KERNEL->>GUI: kernel_main() completado
    deactivate KERNEL
    
    activate GUI
    GUI->>GUI: Carregar Wallpaper padrão
    GUI->>GUI: Renderizar Desktop
    GUI->>GUI: Inicializar Taskbar
    GUI->>GUI: Exibir Welcome screen
    GUI->>GUI: Aguardar entrada do usuário
    deactivate GUI
```

---

##  Fluxo da Interface Gráfica (GUI)

```mermaid
graph TD
    A[" Início: GUI Loop"] --> B[" Aguardar PIT Tick<br/>100 Hz"]
    B --> C[" Processar Entrada"]
    
    C --> D{Keyboard?}
    D -->|Sim| E["Dispatcher de Teclas<br/>S/T/A/C/ESC"]
    E --> F{"Tecla qual?"}
    F -->|S| G["Toggle StartMenu"]
    F -->|T| H["Focus/Open Terminal"]
    F -->|A| I["Open About Window"]
    F -->|C| J["Open Config Window"]
    F -->|ESC| K["Close Active Menu"]
    
    C --> L{Mouse?}
    L -->|Click em Taskbar| M["Aplicação recebe evento"]
    L -->|Click em Janela| N["Window Focus/Drag"]
    L -->|Click em Desktop| O["Ícone Handler"]
    
    G --> P["Renderizar Loop"]
    H --> P
    I --> P
    J --> P
    K --> P
    M --> P
    N --> P
    O --> P
    
    P --> Q[" Limpar Screen Buffer"]
    Q --> R["Renderizar Wallpaper"]
    R --> S["Renderizar Janelas<br/>Z-order"]
    S --> T["Renderizar Taskbar<br/>+ StartMenu (se ativo)"]
    T --> U["Renderizar Cursor Mouse"]
    U --> V[" Double Buffer Swap<br/>Framebuffer → Vídeo"]
    
    V --> W{"Evento<br/>Sair?"}
    W -->|Não| B
    W -->|Sim| X["Finalizar"]
```



---

## Fluxo do Terminal

```mermaid
sequenceDiagram
    participant USER as Usuário
    participant INPUT as Input Handler
    participant PARSER as Command Parser
    participant VFS as Sistema de Arquivos
    participant TERM as Terminal Renderer
    participant OUTPUT as Display
    
    USER->>INPUT: Digita comando
    activate INPUT
    INPUT->>INPUT: Acumula caracteres no buffer
    INPUT->>TERM: Renderiza echo de cada tecla
    INPUT->>OUTPUT: Atualiza display
    
    USER->>INPUT: Pressiona ENTER
    INPUT->>PARSER: Envia buffer completo
    deactivate INPUT
    
    activate PARSER
    PARSER->>PARSER: Tokeniza: comando + args
    PARSER->>PARSER: Busca em command_table[]
    
    alt Comando encontrado
        PARSER->>PARSER: Executa handler correspondente
        
        alt É comando VFS (ls, cd, mkdir, etc)
            PARSER->>VFS: Chama operação VFS
            VFS->>VFS: Navega/modifica árvore
            VFS->>PARSER: Retorna resultado
        end
        
        alt É comando especial (clear, date, mem)
            PARSER->>PARSER: Executa lógica inline
        end
    else Comando não encontrado
        PARSER->>PARSER: "Command not found"
    end
    deactivate PARSER
    
    PARSER->>TERM: Envia saída para renderizar
    activate TERM
    TERM->>TERM: Acumula em history buffer
    TERM->>TERM: Gerencia scroll se necessário
    TERM->>OUTPUT: Renderiza linhas + prompt
    deactivate TERM
```

---

## Hierarquia de Memória

```mermaid
graph TB
    subgraph RAM["RAM Total"]
        KERNEL["Kernel Code/Data<br/>~2 MB"]
        HEAP["Heap Linear<br/>12 MB"]
        UNUSED["Espaço Não Utilizado"]
    end
    
    subgraph HEAP_DETAIL["Heap Manager"]
        ALLOC["Alocações Ativas<br/>kmalloc/kzalloc"]
        INTERNAL["Overhead Interno"]
    end
    
    HEAP --> HEAP_DETAIL
    
    subgraph ALLOCATIONS["Alocações Típicas"]
        FB_BUF["Framebuffer Buffers<br/>2x screen_w×h"]
        WM["Windows Array<br/>8 × sizeof window"]
        VFS["VFS Nodes<br/>32 × sizeof node"]
        COMMANDS["Command History"]
    end
    
    HEAP_DETAIL --> ALLOCATIONS
```

---

## Controladores de Hardware

### Sequência de Interrupção

```mermaid
sequenceDiagram
    participant HW as Hardware<br/>Teclado/Mouse
    participant PIC as PIC<br/>8259A
    participant CPU as CPU
    participant IDT as IDT Handler
    participant KERNEL as Kernel Code
    
    HW->>PIC: Levanta IRQ (e.g., IRQ 1 - kbd)
    activate PIC
    PIC->>CPU: Sinal de Interrupção
    deactivate PIC
    
    activate CPU
    CPU->>CPU: Salva contexto (flags, rip, rsp)
    CPU->>IDT: Busca handler para int vetor
    deactivate CPU
    
    activate IDT
    IDT->>KERNEL: Salta para idt_handler[vetor]
    deactivate IDT
    
    activate KERNEL
    KERNEL->>KERNEL: Salva todos os registradores
    KERNEL->>KERNEL: Executa handler específico
    alt Teclado
        KERNEL->>KERNEL: Lê scancode da porta 0x60
        KERNEL->>KERNEL: Converte para ASCII
        KERNEL->>KERNEL: Acumula em kbd_buffer
    else Mouse
        KERNEL->>KERNEL: Lê status da porta 0x64
        KERNEL->>KERNEL: Lê dados da porta 0x60
        KERNEL->>KERNEL: Atualiza mouse.x, mouse.y
    end
    
    KERNEL->>KERNEL: Envia EOI para PIC
    KERNEL->>KERNEL: Restaura registradores
    KERNEL->>CPU: IRETQ (retorna)
    deactivate KERNEL
    
    activate CPU
    CPU->>CPU: Restaura contexto
    CPU->>HW: Interrupção completa
    deactivate CPU
```

---

## Recursos

### Kernel & Hardware
- **Kernel de 64 bits:** Operação em Long Mode com inicialização via Multiboot2 (GRUB).
- **GDT / IDT:** Tabelas de descritores e tratamento de interrupções configurados manualmente.
- **PIC / PIT:** Controlador de interrupções programável e timer de sistema a 100 Hz.
- **Gerenciamento de Memória:** Heap linear de 12 MB com `kmalloc` / `kzalloc` / `kfree`.
- **RTC:** Leitura do relógio de tempo real do hardware para exibição de data e hora.

### Drivers
- **Framebuffer:** Driver de vídeo direto com suporte a double-buffering (shadow + cache de fundo) para renderização sem flickering.
- **Teclado PS/2:** Driver completo com leitura de scancode e conversão de caracteres.
- **Mouse PS/2:** Captura de posição e botões com snapping de bordas.
- **Fontes & UTF-8:** Renderização de texto com fonte bitmap e conversão UTF-8 → CP437.

### Interface Gráfica (GUI)
- **Gerenciador de Janelas (WM):** Criação, foco, arraste pelo título e fechamento de janelas via mouse.
- **Desktop com Ícones:** Atalhos clicáveis para Terminal, Sobre e Configurações.
- **Taskbar:** Barra de tarefas com botão Iniciar, clock em tempo real e indicador da janela ativa.
- **Menu Iniciar:** Menu pop-up com acesso a aplicativos e opção de reinicialização.
- **Sistema de Wallpaper:** Suporte a gradiente padrão ou imagens convertidas, com três modos de exibição — Preencher, Centralizar e Lado a lado.
- **Cursor de Mouse:** Cursor renderizado em hardware com atualização por frame.
- **Limitador de FPS:** Renderização limitada a ~50 fps via tick do PIT.

### Aplicativos
- **Terminal:** Emulador de console interativo com histórico de comandos (teclas ↑/↓), scroll e cursor piscante.
- **Sobre:** Janela com informações de versão e créditos do sistema.
- **Configurações:** Janela de configuração do papel de parede com seleção de imagem e modo de exibição via teclado e mouse.

### Sistema de Arquivos (VFS)
- **Virtual File System:** Árvore de nós em memória com suporte a arquivos e diretórios.
- Operações disponíveis: criar, listar, navegar, ler, escrever, anexar conteúdo, remover e inspecionar metadados.

---

## Estrutura do Projeto

```text
haos/
├── boot/           # Código de inicialização e transição para Long Mode (ASM)
├── kernel/         # Núcleo: GDT, IDT, PIC, PIT, teclado, memória, RTC
├── drivers/        # Framebuffer, mouse, fonte, UTF-8↔CP437
├── fs/             # Virtual File System (VFS)
├── gui/
│   ├── apps/       # Terminal, Sobre, Configurações
│   ├── elements/   # Taskbar, Menu Iniciar, ícones do desktop
│   ├── screens/    # Boot screen, Welcome screen, Desktop loop
│   ├── gui.c       # Inicialização e loop principal da GUI
│   ├── wallpaper.c # Sistema de wallpaper (gradiente ou imagem)
│   └── window.c    # Gerenciador de janelas (WM)
├── tools/
│   └── img2wallpaper.py  # Ferramenta de conversão de imagens para wallpaper
├── iso/            # Configuração do GRUB para geração da imagem bootável
├── linker.ld       # Script de ligação para organização da memória
└── Makefile        # Automação de compilação e emulação
```

---

## Compilação e Execução

### Dependências (Ubuntu / Debian)

```bash
sudo apt install gcc-x86-64-linux-gnu nasm grub-pc-bin xorriso qemu-system-x86
```

### Comandos Principais

| Comando          | Descrição                                          |
|------------------|----------------------------------------------------|
| `make iso`       | Compila o kernel e gera a imagem `haos.iso`        |
| `make run`       | Inicia emulação via QEMU (GTK → SDL → VNC)         |
| `make run-gtk`   | Força saída via GTK (ideal para WSLg)              |
| `make run-sdl`   | Força saída via SDL                                |
| `make run-vnc`   | Executa sem display, acessível em `localhost:5900` |
| `make run-elf`   | Inicializa diretamente pelo ELF via QEMU           |
| `make debug`     | Modo debug com GDB server na porta 1234            |
| `make clean`     | Remove artefatos de compilação                     |
| `make wallpapers`| Recompila wallpapers convertidos                   |

### Adicionando Wallpapers

Use a ferramenta incluída para converter imagens:

```bash
python3 tools/img2wallpaper.py imagem.png assets/wallpapers/
```

Depois adicione o arquivo `.c` gerado em `WALLPAPER_SRCS` no `Makefile` e defina `-DHAOS_HAS_WALLPAPERS` nos `CFLAGS`.

---

## Atalhos de Teclado

| Tecla     | Ação                                   |
|-----------|----------------------------------------|
| `S`       | Alternar visibilidade do Menu Iniciar  |
| `T`       | Abrir / focar o Terminal               |
| `A`       | Abrir janela Sobre                     |
| `C`       | Abrir janela de Configurações          |
| `ESC`     | Fechar menu ativo                      |
| **Mouse** | Click para focar/arrastar janelas       |

---

## Comandos do Terminal

### Utilitários do Sistema

| Comando   | Descrição                              |
|-----------|----------------------------------------|
| `help`    | Lista todos os comandos disponíveis    |
| `clear`   | Limpa o buffer da tela                 |
| `about`   | Exibe versão e créditos                |
| `date`    | Data e hora atual (via RTC)            |
| `mem`     | Uso de memória da heap                 |
| `reboot`  | Reinicia o hardware                    |
| `echo`    | Imprime texto na saída do terminal     |
| `pwd`     | Exibe o diretório de trabalho atual    |

### Manipulação de Arquivos (VFS)

| Comando              | Descrição                              |
|----------------------|----------------------------------------|
| `ls [dir]`           | Lista conteúdo do diretório            |
| `cd <dir>`           | Navega para o diretório                |
| `mkdir <nome>`       | Cria um novo diretório                 |
| `touch <nome>`       | Cria arquivo vazio                     |
| `write <arq> <texto>`| Escreve conteúdo no arquivo            |
| `append <arq> <texto>`| Adiciona conteúdo ao arquivo          |
| `cat <arq>`          | Exibe o conteúdo do arquivo            |
| `stat <arq>`         | Exibe metadados do nó VFS              |
| `rm <nome>`          | Remove arquivo ou diretório            |

---

## Limitações Técnicas

1. **Volatilidade:** O sistema de arquivos opera estritamente em RAM; dados não são persistidos em disco após reinicialização.
2. **Escalabilidade:** O VFS está limitado a um máximo de 32 nós (arquivos ou diretórios).
3. **Capacidade:** O tamanho máximo por arquivo é de 256 bytes.
4. **Heap:** A memória alocável é limitada a 12 MB e não possui liberação real (`kfree` é no-op).

---

## Referências

- [OSDev Wiki](https://wiki.osdev.org/)
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html)