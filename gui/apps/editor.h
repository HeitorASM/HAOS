// gui/apps/editor.h — Editor de texto simples do HAOS
#pragma once
#include "../window.h"

#define EDITOR_COLS      100     // largura máxima de linha (colunas)
#define EDITOR_MAX_LINES 500     // número máximo de linhas editáveis
#define EDITOR_ROWS      24      // linhas visíveis por vez

// Abre o editor. Se `path` for NULL ou vazio, começa com um documento novo
// e sem nome (ainda não associado a um arquivo do VFS).
// Se `path` existir no VFS, carrega o conteúdo dele.
// Se não existir, cria o arquivo (vazio) na primeira vez que salvar.
Window* editor_create(int32_t x, int32_t y, const char* path);

void editor_tick(Window* win, uint64_t ticks);

// Atualiza o cursor piscante de todas as janelas de editor abertas
// (chamado uma vez por frame pelo loop principal do desktop)
void editor_tick_all(uint64_t ticks);
