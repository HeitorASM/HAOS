# Imagens e o visualizador (`imgview`)

## Como funciona
O visualizador é um **programa de usuário** (`.hexec`, Ring 3), não parte do kernel:
`examples/imgview/` → `/bin/imgview.hexec`. O decodificador (inflate + PNG + BMP) mora em
`examples/imgview/imgdec.h`, em C puro, e roda dentro do processo do visualizador.

O **Explorador de Arquivos** conhece uma tabela de associação extensão → programa
(`k_assoc` em `gui/apps/file_manager.cpp`): `.png` e `.bmp` abrem em `/bin/imgview.hexec`.
O lançamento (`gui/apps/applaunch.cpp`) cria o processo **sem terminal**, com o caminho da
imagem como `argv[1]`.

## Formatos
| Formato | Suporta | Não suporta |
| --- | --- | --- |
| **PNG** | cinza, RGB, paleta (+`tRNS`), cinza+alfa, RGBA; 1/2/4/8/16 bits; filtros 0–4; vários `IDAT` | entrelaçado (Adam7); transparência por cor-chave |
| **BMP** | 1/4/8 bits com paleta; 24 e 32 bits (`BI_RGB`, `BI_BITFIELDS`); de baixo para cima ou de cima para baixo | RLE; 16 bits |

Imagens com transparência aparecem sobre um xadrez. Limite: ~3 megapixels (a heap de um
processo é 32 MiB). JPEG ainda não existe.

## Uso
`f` ajusta à janela · `1` 100% · `+`/`-` ou roda do mouse: zoom · setas ou arrastar: mover ·
`PgDn`/`.` próxima imagem da pasta · `PgUp`/`,` anterior · `q` sai.

## Limite importante: 1 MB por arquivo
O VFS limita cada arquivo a **1 MB** (`VFS_FILE_MAX`). Fotos grandes não cabem; ícones,
capturas de tela e BMPs até ~540×540 (24 bits) cabem. Aumentar esse limite é um passo à parte
(a heap do kernel e o journal de 4 MiB também entram na conta).

## Imagens de exemplo
`/share/images/` recebe, a cada boot, cinco arquivos de exemplo (`gradiente.png`, `circulo.png`,
`paleta.png`, `cinza.png`, `tabuleiro.bmp`), gerados por `python3 tools/gen_samples.py` — que
também gera os valores esperados dos testes. Eles cobrem todos os filtros PNG, vários `IDAT`,
paleta de 4 bits com transparência, RGBA e BMP com *padding*.

## Compilar e testar
```bash
bash tools/build_apps.sh imgview      # gera kernel/generated/imgview_hexec_data.h
make
bash tests/imgview/run.sh             # decodificador no host (com ASan/UBSan)
```
