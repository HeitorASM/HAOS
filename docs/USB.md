# USB (xHCI) — teclado e mouse

## O que funciona
* Controlador **xHCI** (USB 3.x/2.x), por polling (sem IRQ), achado via PCI (classe 0C:03:30).
* **Teclado** e **mouse** HID em *boot protocol*, ligados **direto numa porta** da placa.
  O teclado vira scancodes PS/2 (mesmo caminho do hardware PS/2: layout, Shift,
  Ctrl, Alt+Tab etc. continuam iguais) e ganha repetição de tecla por software
  (500 ms de atraso, depois ~30 ms).
* Plug e desplug em funcionamento (eventos de mudança de porta).
* PS/2 e USB funcionam ao mesmo tempo.

## O que ainda NÃO funciona
* **Hubs USB** (inclusive os embutidos em alguns teclados/docks): o dispositivo atrás deles não aparece.
* Controladores antigos **EHCI/OHCI/UHCI** (PCs de antes de ~2010).
* Mouse com roda e "USB Tablet" (usam *report protocol*; só a interface de boot é usada).
* Teclas multimídia, LEDs (Caps Lock etc.), pendrives.

## Testando no VirtualBox
```
VBoxManage modifyvm "NOME_DA_VM" --usbxhci on --keyboard usb --mouse usb
```
(com a VM desligada). Na interface: Configurações > USB > "USB 3.0 (xHCI)". Para voltar ao
PS/2: `--keyboard ps2 --mouse ps2`.

## Atenção em hardware real
Ao iniciar, o driver **toma posse do controlador da BIOS** (USB Legacy Support). Muitas BIOS
emulam um teclado PS/2 a partir do USB até esse momento. Se o driver não conseguir configurar o
teclado depois disso (hub no meio, teclado exótico), o computador fica **sem teclado**.
Teste primeiro no VirtualBox e tenha um teclado PS/2 ou um teclado ligado direto à placa como plano B.

## Teste de host
```bash
bash tests/usb/run.sh    # tradução HID -> scancodes (não compila o OS)
```
