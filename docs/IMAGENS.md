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
`PgDn`/`.` próxima imagem da pasta · `PgUp`/`,` anterior · `w` papel de parede · `q` sai.

## Papel de parede
* No visualizador, a tecla **`w`** define a imagem aberta como papel de parede.
* No Explorador, botão direito numa imagem → **Definir como papel de parede**
  (roda `imgview --wallpaper <arquivo>`, sem janela).
* O visualizador reduz a imagem (média da área) para caber em **1024×768** mantendo a
  proporção, compõe a transparência sobre preto e chama o syscall `SYS_SET_WALLPAPER`
  (`gfx_set_wallpaper()` na libc). O kernel guarda o resultado em
  `/etc/haos.wallpaper.img` (RGB cru, ≈ 2,3 MB no máximo) e volta com ele no próximo boot —
  inclusive na tela de login.
* No modo **Preencher** a imagem é escalada **sem distorcer** (cobre a tela e corta o excesso,
  centralizado); **Centralizar** e **Lado a lado** usam o tamanho real.
* Menu de contexto do desktop → *Papel de parede ▸ Imagem personalizada* volta a usá-la depois
  de escolher outro. É uma configuração **global** (como o resto das Configurações).

## Tamanho de arquivo
O limite do VFS é **4 MB por arquivo** (`VFS_FILE_MAX`, antes 1 MB). Um sync que mude mais de
~4 MB de uma vez não cabe no journal e cai no modo não atômico (veja ARMAZENAMENTO.md).
Ainda não existe jeito de trazer imagens de fora para o OS — só as de exemplo e as que você criar aqui.

## Ícones no Explorador
O Explorador mostra um ícone por tipo (pasta, programa, imagem, texto), desenhado por código.
**Miniaturas** de imagem exigiriam um decodificador no kernel/GUI (hoje ele só existe dentro do
visualizador) e ficam para depois.

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
