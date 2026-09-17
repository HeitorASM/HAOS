// gui/apps/terminal.h
#pragma once
#include "../core/window.h"

#define TERM_COLS     80
#define TERM_ROWS     24
#define TERM_BUF_SIZE 256
// Histórico de rolagem do terminal. ATENÇÃO AO TAMANHO: este buffer
// é alocado INTEIRO por terminal aberto (TermState é membro direto
// de TerminalWindow), como um bloco contíguo — TERM_HIST *
// (TERM_COLS+1) bytes. Com o valor antigo de 8192 linhas isso dava
// 648 KB POR TERMINAL, o que esgotava/fragmentava a heap do kernel
// (1 MB inicial, expandida em blocos que precisam ser fisicamente
// contíguos) ao abrir o segundo ou terceiro terminal: o kmalloc
// falhava, a janela era construída sobre memória inválida e o
// sistema travava de forma aparentemente aleatória — dependia de
// quanta fragmentação já tinha acontecido até ali.
//
// 512 linhas (~40 KB por terminal) é bastante para rolar bem além
// de uma tela cheia (TERM_ROWS = 24) e cabe com folga mesmo com
// vários terminais abertos ao mesmo tempo. Se algum dia fizer
// sentido ter histórico muito maior, o caminho certo não é aumentar
// este número, e sim alocar o histórico dinamicamente em blocos
// menores (lista de páginas) em vez de um único array contíguo.
#define TERM_HIST     512

Window* terminal_create(int32_t x, int32_t y);

// Cria um terminal novo e imediatamente executa `hexec_path` nele,
// como se o usuário tivesse digitado "run <hexec_path>" — usado
// pelo explorador de arquivos (duplo-clique num .hexec) para que a
// execução sempre tenha um terminal "dono" pronto para mostrar a
// saída, em vez de rodar o processo "às cegas" sem lugar nenhum
// para exibir o que ele escreve via sys_write. Retorna a janela
// criada (sempre não-nula; se o processo falhar ao iniciar, o
// terminal mostra a mensagem de erro normal do comando "run").
Window* terminal_create_and_run(int32_t x, int32_t y, const char* hexec_path);

void terminal_tick(Window* win, uint64_t ticks);
