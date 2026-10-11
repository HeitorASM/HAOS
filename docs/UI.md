# Toolkit de interface para apps de usuário

Objetivo: apps `.hexec` com botões, campos, listas, menus e diálogos **sem desenhar tudo
pixel a pixel**, seguindo o tema do sistema. O estilo escolhido é o de **objetos com
callbacks**; uma linguagem/scripting fica para o futuro, por cima desta camada.

## Onde estamos
| Etapa | Conteúdo | Estado |
| --- | --- | --- |
| **3A — Fundação** | renderizador por software, tema, título, clipboard, fonte, evento de tema | **pronta** |
| 3B — Widgets | layout automático (caixas H/V), rótulo, botão, caixas, campos de texto, lista, abas, menus, diálogos | próxima |
| 3C — Prova | reescrever `winpaint` e `imgview` com o toolkit + guia "seu primeiro app" | depois |
| 3D — Integração | manifesto do app (nome, ícone, extensões) no menu Iniciar e no Explorador | depois |

## Etapa 3A — o que existe

### Desenhar no próprio app (`haos/draw.h`)
O app mantém um canvas **seu** (buffer na heap) e envia tudo com `cv_present()`: **um syscall
por quadro**, em vez de um por retângulo.

```c
#include <haos/gfx.h>
#include <haos/draw.h>

cv_t cv;  cv_init(&cv, 400, 300);
cv_fill(&cv, 0, 0, 400, 300, 0x101830);
cv_fill_round(&cv, 20, 20, 120, 32, 6, 0x2A6FD1);        // botão arredondado
cv_text(&cv, 30, 28, "Somar", 0xFFFFFF, CV_NO_BG);
cv_present(&cv, win);
```
Formas (`cv_fill`, `cv_rect`, `cv_line`, `cv_fill_round`, `cv_round`), texto (`cv_text`, fonte 8×16 CP437
do kernel, lida uma vez), imagens (`cv_blit`, `cv_blit_alpha`) e **recorte** (`cv_set_clip`) — tudo
recortado ao canvas sem erro.

### Tema do sistema (`haos/uitheme.h`)
```c
ui_theme_t th;  ui_theme_load(&th);
cv_fill(&cv, 0, 0, w, h, th.c[UI_WINDOW_BG]);
// no laço de eventos:
if (ev.type == HAOS_EV_THEME) { ui_theme_load(&th); redesenha(); }
```
33 entradas: fundo, painéis, texto (3 níveis), destaque, botões, campos, caixas, listas e métricas
(espaçamentos, altura de widget, raio). O kernel manda `HAOS_EV_THEME` a todas as janelas do app
quando o tema muda.

### Syscalls novos
| Syscall | libc | Para quê |
| --- | --- | --- |
| `SYS_FONT_DATA` (34) | `gfx_font_data()` | tabela de glifos do kernel (uma só fonte de verdade) |
| `SYS_THEME_GET` (35) | `gfx_theme_get()` / `ui_theme_load()` | cores e métricas do tema |
| `SYS_WIN_SET_TITLE` (36) | `win_set_title()` | título dinâmico da janela |
| `SYS_CLIPBOARD` (33) | `clip_get()` / `clip_set()` | texto do sistema, até 64 KiB, **compartilhado com o Editor** |

## Testes
```bash
bash tests/libui/run.sh     # renderizador cv_* no host (ASan/UBSan)
```
