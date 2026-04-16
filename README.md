# HAOS — Home-built Operating System v1.1

[![Architecture](https://img.shields.io/badge/Arch-x86__64-blue)]()
[![Language](https://img.shields.io/badge/Lang-C%20%2B%20ASM-orange)]()

O HAOS é um sistema operacional bare-metal de 64 bits desenvolvido de forma independente e educacional. O projeto visa a implementação de um núcleo funcional em arquitetura x86\_64, utilizando C freestanding e Assembly, sem dependências de bibliotecas externas ou do sistema hospedeiro.

-----

## Recursos

  * **Kernel de 64 bits:** Operação em Long Mode com gerenciamento de GDT, IDT e paginação.
  * **Multitarefa:** Sistema de multitasking cooperativo para gerenciamento de processos.
  * **Interface Gráfica:** Gerenciador de janelas com suporte a eventos de mouse e renderização de fontes.
  * **Sistema de Arquivos:** Virtual File System (VFS) para abstração e manipulação de dados em memória.
  * **Shell Interativo:** Console para execução de comandos e diagnóstico do sistema.
  * **Drivers de Entrada:** Implementação de drivers para teclado PS/2 e mouse.

-----

## Estrutura do Projeto

```text
OS/
├── boot/       # Código de inicialização e transição de modo (ASM)
├── kernel/     # Núcleo do sistema e gerenciamento de interrupções
├── fs/         # Implementação do Virtual File System (VFS)
├── drivers/    # Drivers de hardware (Teclado, Mouse, Framebuffer)
├── gui/        # Interface gráfica e gerenciador de janelas
├── iso/        # Arquivos de configuração para geração da imagem ISO
├── linker.ld   # Script de ligação para organização da memória
└── Makefile    # Automação do processo de compilação e emulação
```

-----

## Procedimentos de Compilação

### Dependências

Para ambientes baseados em Ubuntu ou Debian:

```bash
sudo apt install gcc-x86-64-linux-gnu nasm grub-pc-bin xorriso qemu-system-x86
```

### Comandos Principais

  * **Compilação:** `make iso` gera a imagem bootável.
  * **Execução Geral:** `make run` inicia a emulação via QEMU.
  * **Ambiente WSLg:** `make run-gtk` força a saída via GTK.
  * **Modo Remoto:** `make run-vnc` permite conexão via localhost:5900.

-----

## Interface e Atalhos de Teclado

A interação com o sistema é realizada através dos seguintes comandos globais:

  * **[S]**: Alternar visibilidade do Menu Iniciar.
  * **[T]**: Abrir uma nova instância do Terminal.
  * **[A]**: Exibir informações do sistema (Sobre).
  * **[ESC]**: Fechar menus ativos.
  * **Mouse**: Arrastar janelas pela barra de título ou fechá-las através do ícone correspondente.

-----

## Comandos do Terminal

### Utilitários de Sistema

  * `help`: Lista os comandos disponíveis.
  * `clear`: Limpa o buffer da tela.
  * `about`: Exibe informações de versão e créditos.
  * `date`: Exibe a data e hora atual.
  * `mem`: Informações sobre o uso de memória.
  * `reboot`: Reinicia o hardware.
  * `color`: Exibe a paleta de cores do sistema.

### Manipulação de Arquivos (VFS)

  * `ls` / `dir`: Listagem de diretórios.
  * `mkdir` / `cd`: Criação e navegação de diretórios.
  * `touch` / `write` / `append`: Criação e edição de conteúdo.
  * `cat` / `stat` / `rm`: Leitura, metadados e exclusão de arquivos.

-----

## Limitações Técnicas

O HAOS encontra-se em fase de desenvolvimento educacional, apresentando as seguintes restrições:

1.  **Volatilidade:** O sistema de arquivos é virtual e opera estritamente em RAM; dados não são persistidos em disco.
2.  **Escalabilidade:** O VFS está limitado a um máximo de 32 nós (arquivos ou diretórios).
3.  **Capacidade:** O tamanho máximo permitido por arquivo é de 256 bytes.