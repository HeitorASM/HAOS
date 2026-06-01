
# Conteúdo dedicado estritamente aos diagramas e arquitetura detalhada

Este arquivo centraliza a modelagem de engenharia e diagramas de sequência do **HAOS**. Se você deseja entender como o sistema gerencia o hardware e renderiza componentes de software sob uma ótica interna, este guia descreve esses fluxos de maneira clara.

---

## 1. Arquitetura do Sistema e Componentes

Abaixo está o mapeamento estrutural detalhado de como os diferentes subsistemas interagem, desde a camada mais baixa de hardware até os aplicativos em C++ que rodam no espaço gráfico.

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
        KMEM["Kernel Heap Manager<br/>C++ OOP / Coalescing Heap"]
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
        WM["Window Manager<br/>C++ OOP Hierarchy"]
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

## 2. Fluxo de Inicialização (Boot Sequence)

Este diagrama mostra o ciclo de vida da inicialização a partir do firmware da máquina até a liberação completa da interface gráfica pronta para o usuário.

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

## 3. Fluxo Interno da Interface Gráfica (GUI Loop)

O gerenciamento de eventos de periféricos (Teclado e Mouse) e a pilha de renderização em camadas (*Z-Order*) com *Double Buffering* funcionam em ciclos síncronos:

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

## 4. Subsistema do Terminal e Interação com VFS

Este diagrama de sequência ilustra como a entrada bruta de texto do teclado é capturada, tratada pelo interpretador e integrada dinamicamente com o barramento do Sistema de Arquivos Virtual:

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

## 5. Gerenciamento e Estrutura da Heap Dinâmica

Como a nova estrutura em C++ organiza os buffers gráficos e os nós alocados pelo kernel de forma eficiente na memória RAM do sistema:

```mermaid
graph TB
    subgraph RAM["RAM Total"]
        KERNEL["Kernel Code/Data<br/>~2 MB"]
        HEAP["Kernel Heap Dinâmica<br/>Alocação Dinâmica C++"]
        UNUSED["Espaço Não Utilizado"]
    end
    
    subgraph HEAP_DETAIL["KernelHeap Manager"]
        ALLOC["Alocações Ativas<br/>new / kmalloc"]
        FREE_LIST["Lista de Blocos Livres<br/>First-Fit + Coalescing"]
        INTERNAL["Overhead de Metadados"]
    end
    
    HEAP --> HEAP_DETAIL
    
    subgraph ALLOCATIONS["Alocações Dinâmicas (GUI & VFS)"]
        FB_BUF["Framebuffer Buffers<br/>2x screen_w×h"]
        WM["Hierarquia de Widgets C++<br/>Window2, Button, etc."]
        VFS["VFS Nodes Ativos"]
        COMMANDS["Command History"]
    end
    
    HEAP_DETAIL --> ALLOCATIONS

```


## 6. Tratamento de Interrupções de Hardware (Hardware Interrupts)

O ciclo de vida completo de um sinal gerado fisicamente pelos periféricos PS/2 (Teclado/Mouse) passando pela CPU e sendo tratado pelos rotinas de interrupção (ISRs):

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
