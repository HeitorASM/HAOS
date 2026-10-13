# Armazenamento: IDE, SATA/AHCI e partições

## O que o HAOS suporta agora

| Controlador | Como | Observação |
| --- | --- | --- |
| IDE/ATA (Primary Master) | PIO, `rep insw/outsw` | como antes, só que bem mais rápido em VM |
| **SATA/AHCI** | DMA, 48-bit LBA, polling | até 4 discos; setor lógico de 512 B |

Não há ainda NVMe (muitos notebooks novos só têm NVMe) nem USB.
Na BIOS, o modo do SATA precisa ser **AHCI** (não "RAID/RST" nem "IDE").

## Regras de segurança (`drivers/storage.c`)

O HAOS **nunca escreve num disco que não é dele**. Para cada disco, em ordem:

1. Tem **partição HAOS** (MBR tipo `0xDA` ou GPT com o GUID abaixo) → usa só ela.
2. Tem HAOSFS válido no disco inteiro (formato antigo, VDI atual) → usa.
3. Está **completamente em branco** (setor 0 zerado, sem tabela) → usa e formata.
4. Qualquer outro caso (Windows, Linux, tabela sem partição HAOS…) → **não toca**;
   o HAOS roda só em RAM.

GUID de tipo GPT: `48414F53-4653-4841-4F53-465300000001`.

## Instalando num disco real

1. Crie uma partição de ≥ 64 MiB com tipo `0xDA` (MBR) ou o GUID acima (GPT),
   p.ex. `sgdisk -n 1:0:+1G -t 1:48414F53-4653-4841-4F53-465300000001 /dev/sdX`.
2. Zere o começo dela (o HAOSFS começa 1 MiB dentro da partição):
   `dd if=/dev/zero of=/dev/sdX1 bs=1M count=2`.
3. Boot: o HAOS acha a partição, vê que está vazia e formata **só ela**.

## Por que o sync travava (e o que mudou)

* Cada `vfs_sync` regravava o disco todo (inodes 256 KiB, bitmap, todos os arquivos)
  e cada setor custava 256 `outw` (cada um é uma saída da VM). Agora:
  * `rep insw/outsw` — uma instrução por setor;
  * `haosfs_save` guarda um hash por bloco e **só grava blocos que mudaram**;
  * AHCI move até 64 KiB por comando via DMA.
* Antes do superbloco novo, o disco recebe um `FLUSH CACHE` (dados na mídia
  primeiro, "confirmação" depois). Ainda **não é journaling**: queda de energia
  no meio de um sync pode corromper; isso é o próximo item.
* Volumes **novos** são limitados a 1 GiB (bitmap pequeno); discos já formatados
  mantêm o tamanho que têm.

## Limitações

* Um comando por vez, sem IRQ, sem NCQ; sem trava entre tarefas.
* Partições estendidas MBR (lógicas) não são lidas; CRC da GPT não é conferido (só leitura).
* Setores lógicos de 4 KiB (4Kn) são recusados.

## Testes (no host, sem compilar o OS)

```bash
bash tests/partition/run.sh   # MBR/GPT com disco simulado
bash tests/perm/run.sh        # contas/permissões/login
```
