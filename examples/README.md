# examples/ — programas de usuário escritos contra a libc do HAOS

Cada subpasta é um programa HEXEC em C usando `libc/`. Servem de produto
(os dois primeiros vêm com o OS) e de referência para escrever os seus.

| Pasta | O que é | Embutido no OS? |
|---|---|---|
| `winpaint/` | Programa de pintura em janela própria (`SYS_WIN_*`) | Sim |
| `imgview/`  | Visualizador de imagens PNG/BMP (ver `docs/IMAGENS.md`) | Sim |
| `uidemo/`   | Demo do toolkit de UI (`haos/ui.h`, ver `docs/UI.md`) | Não — opcional |

## Compilar e embutir

Nada gerado fica no git. Os `.hexec` e os headers em `kernel/generated/`
são criados por:

```bash
make apps                         # winpaint + imgview + imagens de exemplo
# ou, só os apps:
bash tools/build_apps.sh          # padrão: winpaint imgview
bash tools/build_apps.sh winpaint imgview uidemo   # inclui a demo de UI
```

Depois, `make` / `make iso`. O `fs/vfs.c` embute em `/bin` os apps cujo header
existir em `kernel/generated/`. Rode no HAOS com `run winpaint.hexec` etc.

## Criar um app novo

1. Crie `examples/<nome>/main.c` (use só os headers de `libc/include`).
2. `bash tools/build_apps.sh <nome>`.
3. Para embutir por padrão: adicione o `__has_include` e a entrada em
   `g_builtin_bins` em `fs/vfs.c`, e o nome no padrão do `tools/build_apps.sh`.

O `-mcmodel=large` é obrigatório (o endereço de carga fica acima de 4 GB) —
o `build_apps.sh` já passa a flag. Detalhes em `libc/README.md`.
