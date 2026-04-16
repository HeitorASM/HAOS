Kernel / Core

Suporte a ACPI / RTC
Implementar leitura do Real-Time Clock para exibir data/hora real no terminal e na taskbar (atualmente é simulado com ticks).
Feito

Gerenciador de memória paginada
Substituir o alocador bump atual por um alocador de páginas com malloc/free real e proteção de memória.

Multitasking cooperativo ou preemptivo
Permitir que múltiplos programas rodem simultaneamente (mesmo que apenas em threads kernel).

Suporte a símbolos e módulos carregáveis
Permitir que novos aplicativos sejam compilados separadamente e carregados dinamicamente (ELF loader).

Tratamento de exceções (Page Fault, GPF, etc.)
Exibir mensagens amigáveis em vez de travar o sistema.

Drivers / Hardware
Suporte a USB básico (UHCI/OHCI)
Habilitar teclados/mouses USB (hoje apenas PS/2 funciona).

Suporte a placa de rede (ex: RTL8139 ou E1000)
Adicionar stack TCP/IP mínima (lwIP) e comandos como ping, wget.

Driver de áudio (ex: Intel HD Audio ou Sound Blaster 16)
Reproduzir tons simples ou arquivos WAV.

Suporte a ATA/ATAPI (PIO mode)
Ler setores de disco rígido real ou CD-ROM, base para sistema de arquivos.

Modos gráficos nativos com VBE
Utilizar VBE para obter resoluções maiores que 1024x768 (ex: 1366x768, 1920x1080).

Suporte a hotkeys de sistema
Ctrl+Alt+Del para reiniciar, Alt+Tab para trocar janelas.

Interface Gráfica (GUI)
Redimensionamento de janelas
Adicionar cantos arrastáveis para redimensionar janelas (atual apenas move).

Botões minimizar/maximizar funcionais
Minimizar ícone na taskbar, maximizar para tela cheia.

Cursor de texto (I-beam) em campos de entrada
Mudar sprite do mouse quando sobre área editável.

Scrollbars
Adicionar barras de rolagem no terminal e outras janelas com conteúdo longo.

Temas e personalização de cores
Arquivo de configuração para alterar paleta de cores da GUI.

Suporte a arrastar ícones
Mover ícones do desktop livremente.

Menu de contexto (botão direito)
Exibir opções como "Novo arquivo", "Propriedades", "Atualizar".

Papel de parede personalizável
Carregar uma imagem BMP simples como fundo.

Fontes escaláveis ou tamanhos variáveis
Permitir usar fontes 8x8, 8x16 ou até 16x32 para acessibilidade.

Aplicativos
Editor de texto simples
Um bloco de notas integrado (ex: edit) para criar/editar arquivos.

Visualizador de imagens BMP
Exibir imagens no framebuffer.

Calculadora
Aplicativo com operações básicas (+, -, *, /).

Gerenciador de tarefas
Listar processos/janelas abertas e permitir finalizar.

Configurações reais
Substituir o placeholder config.c por uma janela com opções reais (resolução, tema, etc.).

Shell com histórico de comandos
Setas para cima/baixo navegam pelo histórico do terminal.

Comando help com paginação
Exibir ajuda página por página (pressione espaço para continuar).

Jogos simples
Ex: Campo Minado, Snake ou Tetris para demonstrar capacidades gráficas.

Sistema / Usabilidade
Salvar configurações em disco
Persistir preferências do usuário (papel de parede, atalhos) entre reinicializações.

Suporte a teclado ABNT2 (Ç, acentos) completo
Mapear corretamente teclas como ', ", ~, ^ no layout brasileiro.

Clipboard entre aplicações
Copiar e colar texto entre terminal e editor.

Proteção de tela (screensaver)
Após inatividade, exibir animação ou desligar backlight.

Log de sistema
Salvar mensagens de kernel panic/depuração em arquivo para análise posterior.

Instalador para criar um Live USB
Script para gerar imagem gravável em pendrive e boot direto no hardware real.