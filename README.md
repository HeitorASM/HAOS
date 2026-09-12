# HAOS

[![Architecture](https://img.shields.io/badge/Arch-x86__64-blue)](https://en.wikipedia.org/wiki/X86-64)
[![Language](https://img.shields.io/badge/Lang-C%20%2B%20ASM%20%2B%20C%2B%2B-orange)]()
[![Boot](https://img.shields.io/badge/Boot-Multiboot2%20%2B%20GRUB-green)]()

O HAOS é um sistema operacional bare-metal de 64 bits desenvolvido de forma independente e educacional. O projeto visa a implementação de um núcleo funcional em arquitetura x86_64, utilizando C freestanding, Assembly e C++, sem dependências de bibliotecas externas ou do sistema hospedeiro.

---

![HAOS rodando no VirtualBox](assets/README/VirtualBox_HAOS.png)

> Interface do sistema em execução

---

## Recursos Implementados

### Kernel & Hardware
- **Kernel de 64 bits:** Operação em Long Mode com inicialização via Multiboot2 (GRUB).
- **GDT / IDT:** Tabelas de descritores e tratamento de interrupções configurados manualmente.
- **PIC / PIT:** Controlador de interrupções programável e timer de sistema a 100 Hz.
- **Gerenciamento Avançado de Memória:**
  - **PageFrameAllocator (PFA):** Alocação de páginas físicas via bitmap, usando o mapa de memória do Multiboot2.
  - **KernelHeap:** Alocador *First-Fit* com coalescência automática (fusão de blocos livres adjacentes).
  - **Operadores globais new/delete:** Sobrecarga completa para alocação dinâmica em C++ (`new`, `new[]`, `delete`, `delete[]`).
  - **API C:** `kmalloc`, `kzalloc`, `kfree` e utilitários de string (`kstrcpy`, `kstrcat`, `kitoa`, etc.).
- **RTC:** Leitura do relógio de tempo real do hardware para exibição de data e hora.
- **Sysinfo:** Detecção de CPU via instrução CPUID (vendor, brand string e número de núcleos).

### Drivers
- **Framebuffer:** Driver de vídeo direto com double-buffering (shadow + cache de fundo) para renderização sem flickering.
- **Teclado PS/2:** Driver completo com leitura de scancode, conversão de caracteres, suporte a Shift, Caps Lock, Ctrl e setas (com e sem Shift para seleção de texto).
- **Mouse PS/2:** Captura de posição e botões com snapping de bordas.
- **Fontes & UTF-8:** Renderização de texto com fonte bitmap 8×16 (CP437) e conversão UTF-8 → CP437
- **RTC:** Leitura e formatação de data/hora em tempo real.
- **Armazenamento ATA/IDE:** Leitura e escrita PIO em setores de 512 bytes, com identificação LBA28, polling, timeout e tratamento de erros.

### Interface Gráfica (GUI)
- **Gerenciador de Janelas (WM):** Sistema de janelas com foco, arraste pelo título, minimização, fechamento e ordem de empilhamento.
- **Widgets OOP (C++):** Hierarquia orientada a objetos com `Widget` (base abstrata), `Button`, `Label` e `Window` (contêiner).
- **Desktop com Ícones:** Atalhos clicáveis para Terminal, Sobre, Configurações, Bloco de Notas e Explorador de Arquivos.
- **Taskbar:** Barra de tarefas com botão Iniciar, clock em tempo real e indicador da janela ativa.
- **Menu Iniciar:** Menu pop-up com acesso a aplicativos e opção de reinicialização.
- **Explorador de Arquivos:** Navegação pela árvore VFS, retorno ao diretório pai ou à raiz, seleção de itens e abertura de arquivos textuais no Bloco de Notas por duplo clique.
- **Sistema de Wallpaper:** Suporte a gradiente padrão ou imagens convertidas, com três modos de exibição — Preencher, Centralizar e Lado a lado.
- **Cursor de Mouse:** Cursor renderizado em hardware com atualização por frame.
- **Limitador de FPS:** Renderização limitada a ~50 fps via tick do PIT.

### Aplicativos
- **Terminal:** Emulador de console interativo com histórico de comandos (teclas ↑/↓), scroll, cursor piscante e suporte a comandos de sistema e VFS.
- **Bloco de Notas (Editor):** Editor de texto multi-linha com cursor navegável (setas), seleção de texto (Shift+setas e mouse), copiar/colar/recortar (Ctrl+C/V/X), selecionar tudo (Ctrl+A), salvar (Ctrl+S ou F2) e diálogo "salvar como" integrado ao VFS.
- **Sobre:** Janela com informações de versão, arquitetura, boot, vídeo, GUI, input e kernel.
- **Configurações:** Janela para seleção de wallpaper, modo de exibição e idioma (Português/English), com informações de hardware (CPU, RAM, heap).
- **Internacionalização dos aplicativos:** Textos do Explorador, do desktop, do terminal e das demais janelas são obtidos das tabelas de tradução em Português/Inglês.

### Sistema de Arquivos (VFS)
- **Virtual File System:** Árvore de nós em memória com suporte a arquivos e diretórios.
  - **Limites:** Nome de arquivo: 64 caracteres; Máx. arquivos por diretório: 128; Tamanho máximo por arquivo: 1 MB (crescimento dinâmico).
- **Operações disponíveis:** criar, listar, navegar, ler, escrever, anexar conteúdo, remover e inspecionar metadados.
- **HAOSFS persistente:** Superbloco, bitmap de blocos, tabela de inodes e extents armazenados em disco.
- **Montagem automática:** Volumes válidos são montados no boot; discos vazios são formatados uma única vez; volumes desconhecidos não são sobrescritos.
- **Persistência:** Alterações são gravadas imediatamente e `reboot` executa `vfs_sync()` antes de reiniciar.

### Internacionalização (i18n)
- **Suporte a múltiplos idiomas:** Português (Brasil) e Inglês, com alternância em tempo de execução.
- **Tabelas de strings centralizadas:** Todas as mensagens da UI e do terminal são traduzíveis via `tr(STR_ID)`.

---

## Estrutura do Projeto

```
├── boot/           # Código de inicialização e transição para Long Mode (ASM)
├── kernel/         # Núcleo: GDT, IDT, PIC, PIT, teclado, memória, RTC
├── drivers/        # Framebuffer, mouse, fonte, UTF-8↔CP437, bloco e ATA/IDE
├── fs/             # VFS e filesystem persistente HAOSFS
├── gui/
│   ├── apps/       # Terminal, Explorador, Bloco de Notas, Sobre e Configurações
│   ├── elements/   # Taskbar, Menu Iniciar e ícones do desktop
│   ├── screens/    # Tela de boot, login e loop do desktop
│   ├── gui.cpp     # Inicialização da GUI em C++
│   ├── wallpaper.c # Sistema de wallpaper 
│   └── core/       # Widgets, layouts, janelas e gerenciador de janelas
├── tools/
│   └── img2wallpaper.py  # Ferramenta de conversão de imagens para wallpaper
├── iso/            # Configuração do GRUB para geração da imagem bootável
├── linker.ld       # Script de ligação para organização da memória 
└── Makefile        # Automação de compilação e geração da ISO
```

---

## Compilação e Execução

### Dependências (Ubuntu / Debian)

```bash
sudo apt install gcc-x86-64-linux-gnu g++-x86-64-linux-gnu nasm grub-pc-bin xorriso python3-pip
pip3 install Pillow
```

### Comandos Principais

| Comando | Descrição |
| --- | --- |
| `make` ou `make all` | Compila e gera `haos.elf` |
| `make iso` | Compila o kernel e gera a imagem `haos.iso` |
| `make clean` | Remove artefatos de compilação |
| `make wallpapers` | Recompila wallpapers convertidos |

### Adicionando Wallpapers

Use a ferramenta incluída para converter imagens:

```bash
# Converte uma única imagem
python3 tools/img2wallpaper.py assets/minha_imagem.png

# Converte todas as imagens de uma pasta
python3 tools/img2wallpaper.py assets/imagens/

# Especifica resolução (padrão: 1024×768)
python3 tools/img2wallpaper.py imagem.png --width 1920 --height 1080
```

Após converter, adicione os arquivos `.c` gerados ao `Makefile` em `WALLPAPER_SRCS` e compile com `make wallpapers`.

---

## Atalhos de Teclado

| Tecla | Ação |
| --- | --- |
| `ESC` | Fechar o menu ativo ou a janela focada |
| `Backspace` | Voltar ao diretório pai no Explorador |
| **Mouse** | Abrir aplicativos pelo desktop/Menu Iniciar, selecionar itens, navegar no Explorador e focar/arrastar janelas |

O Explorador também possui os botões `Voltar` e `Início`. O duplo clique em um
arquivo textual abre o conteúdo no Bloco de Notas; arquivos com byte nulo são
tratados como binários e não são abertos pelo editor.

### Editor de Texto (Bloco de Notas)

| Tecla | Ação |
| --- | --- |
| `←/↑/↓/→` | Mover cursor |
| `Shift+←/↑/↓/→` | Estender seleção |
| `Ctrl+A` | Selecionar todo o texto |
| `Ctrl+C` | Copiar seleção |
| `Ctrl+X` | Recortar seleção |
| `Ctrl+V` | Colar |
| `Ctrl+S` / `F2` | Salvar arquivo |
| `ESC` | Fechar (ou cancelar "salvar como") |

---

## Comandos do Terminal

### Utilitários do Sistema

| Comando | Descrição |
| --- | --- |
| `help` | Lista todos os comandos disponíveis |
| `clear` | Limpa o buffer da tela |
| `about` | Exibe versão e créditos |
| `date` | Data e hora atual |
| `mem` | Uso em tempo real da Heap do Kernel |
| `reboot` | Reinicia o hardware |
| `echo <texto>` | Imprime texto na saída do terminal |
| `lang <pt\|en>` | Altera o idioma do sistema |

### Manipulação de Arquivos (VFS)

| Comando | Descrição |
| --- | --- |
| `pwd` | Exibe o diretório de trabalho atual |
| `ls [dir]` | Lista conteúdo do diretório |
| `cd <dir>` | Navega para o diretório |
| `mkdir <nome>` | Cria um novo diretório |
| `touch <nome>` | Cria arquivo vazio |
| `write <arq> <texto>` | Escreve conteúdo no arquivo |
| `append <arq> <texto>` | Adiciona conteúdo ao arquivo |
| `cat <arq>` | Exibe o conteúdo do arquivo |
| `stat <arq>` | Exibe metadados do nó VFS |
| `rm <nome>` | Remove arquivo ou diretório |
| `edit <nome>` | Abre o Bloco de Notas com o arquivo |

---

## Armazenamento Persistente

O HAOSFS usa o disco inteiro a partir do LBA 2048, preservando a região inicial
do bootloader. A primeira implementação suporta discos ATA/IDE apresentados
como Primary Master e foi validada no VirtualBox com uma imagem VDI dedicada.

No VirtualBox, use boot BIOS com `Enable EFI` desmarcado e conecte o disco de
dados ao controlador IDE. A ISO contém o kernel e o GRUB; o VDI contém os
arquivos persistentes.

## Limitações Técnicas Atuais

1. **Escalabilidade do FS:** Limite de 128 itens por diretório e 1 MB por arquivo.
2. **Controlador:** O driver atual suporta ATA/IDE PIO, não AHCI/SATA nativo.
3. **Particionamento:** O HAOSFS usa o volume inteiro e ainda não lê MBR ou GPT.
4. **Robustez:** Ainda não há journaling nem recuperação garantida se a energia for interrompida durante uma gravação.
5. **Isolamento de Processos:** O sistema opera integralmente em Ring 0 (Kernel Mode) sem separação de espaço de usuário e multitarefa preemptiva.
6. **Drivers:** Ainda não há suporte a USB, PCI, ACPI avançado ou som.

---

## Documentação Adicional

Para entender os fluxos de inicialização, a arquitetura visual interna e o funcionamento dos controladores, veja o arquivo [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Referências

* [OSDev Wiki](https://wiki.osdev.org/)
* [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html)
* [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
* [CPUID Instruction](https://en.wikipedia.org/wiki/CPUID)
