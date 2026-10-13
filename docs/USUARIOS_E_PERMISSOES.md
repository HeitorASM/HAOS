# Usuários e permissões

Esta fase transforma o login — que era só uma tela com um nome e uma senha
opcional — num modelo de contas de verdade, com isolamento entre usuários.

## Resumo

| Antes | Agora |
| --- | --- |
| 1 usuário em `/etc/user.cfg` | Várias contas em `/etc/passwd` |
| Senha = FNV-1a de 32 bits (quebrável em segundos) | Senha = **PBKDF2-HMAC-SHA-256**, 10 000 iterações, salt aleatório de 16 bytes |
| Qualquer programa lia o hash da senha | `/etc/passwd` é `root:root 0600`: nenhum usuário comum o lê |
| Todos os arquivos acessíveis a todos | Cada nó do VFS tem **dono, grupo e permissões** (`rwxrwxrwx` + sticky) |
| Programas `.hexec` sem identidade | Cada processo tem `uid`/`gid` e **nunca roda como root** |
| Pasta pessoal fixa `/home/user` | `/home/<nome>`, modo `0700` |

## Modelo de contas

* **`root` (uid 0)** — dono dos arquivos do sistema. A conta é **travada**: ninguém
  entra como root, e nenhum processo de usuário roda com uid 0.
* **Usuários (uid ≥ 1000)** — o primeiro, criado no primeiro boot, é **administrador**.
* **Administrador** — no terminal/GUI ignora as permissões de arquivo (equivale a
  root), cria/remove contas, redefine senhas e muda donos. Os **programas** que ele
  inicia continuam **sem privilégio**.
* Cada conta tem um grupo próprio (`gid == uid`); ainda não há grupos compartilhados.

## Permissões do VFS (`fs/perm.h`)

Regras, como no Unix:

* Atravessar um diretório exige **X** nele — por isso `/home/ana` com `0700` esconde
  tudo que está dentro, até arquivos `0644`.
* Listar exige **R** no diretório; criar/remover entradas exige **W+X** nele.
* Ler um arquivo exige **R**; escrever exige **W**; **rodar um `.hexec` exige R**.
* Vale **uma única classe**: se você é o dono, valem só os bits do dono, mesmo que
  "outros" tenha mais permissão.
* **Sticky (`01000`)**, usado em `/tmp` (`1777`): só o dono do arquivo, o dono do
  diretório ou o administrador remove entradas.
* Remover um diretório confere **cada item de dentro** (senão bastaria embrulhar
  arquivos alheios numa pasta sua para apagá-los).
* `chmod`: dono ou administrador. `chown`: só administrador.

A checagem é imposta em **todas** as portas de entrada: syscalls de arquivo
(Ring 3), terminal, editor e explorador de arquivos. Código de kernel "de sistema"
(carregar tema, idioma, wallpaper) usa a API sem sufixo `_as` e não checa nada —
só o kernel chega a ela; um programa de usuário só alcança o VFS por syscalls.

## Formato de `/etc/passwd`

```
nome:uid:gid:flags:home:salt:hash
```

* `flags`: `A` administrador, `L` conta travada, `-` nenhuma.
* `salt`: 32 dígitos hex; **vazio = conta sem senha**.
* `hash`: 64 dígitos hex.
* Linhas inválidas, nomes ou uids duplicados são ignorados na leitura; `root`
  sempre existe, e o modo `0600` é reforçado a cada boot.

## Persistência (`HaosInode`)

`uid`, `gid` e `mode` ficam no espaço antes "reservado" do inode. O inode continua
com **256 bytes** (há um `_Static_assert`) e o layout dos campos antigos não mudou,
então **discos formatados antes desta versão montam normalmente**: nós sem a marca
`PRM1` recebem `root:root 0755/0644`.

## Migração automática

Se existir o `/etc/user.cfg` da versão anterior:

1. a conta vira o primeiro usuário (administrador, uid 1000), com o mesmo nome
   (caracteres inválidos viram `_`);
2. a pasta antiga `/home/user` é **mantida** (agora de 1000, modo `0700`);
3. o hash FNV-1a antigo é aceito **uma vez** e, no primeiro login correto, trocado
   por PBKDF2 com salt; o `user.cfg` é apagado do disco.

## Terminal

| Comando | Descrição |
| --- | --- |
| `whoami` | Usuário atual |
| `id` | uid, gid e se é administrador |
| `users` | Lista as contas |
| `adduser <nome> [admin]` | Cria conta (só administrador) — pede a senha mascarada |
| `deluser <nome>` | Remove conta (só administrador). A home é mantida, passa para root |
| `passwd [nome]` | Troca a senha (pede a atual ao trocar a própria) |
| `chmod <octal> <nome>` | Ex.: `chmod 600 segredo.txt` (só o dono) |
| `chown <usuário> <nome>` | Troca o dono (só administrador) |
| `ls -l [dir]` | Mostra permissões e dono |

`cd` sem argumento vai para a pasta pessoal. `ESC` cancela um prompt de senha.

## API para programas (`libc`)

`getuid()`, `getgid()`, `chmod(path, modo)` — syscalls `SYS_GETUID` (27),
`SYS_GETGID` (28), `SYS_CHMOD` (29). As syscalls de arquivo existentes agora
devolvem `-1` quando faltar permissão.

## Testes (no host, sem compilar o OS)

```bash
bash tests/crypto/run.sh   # SHA-256 / PBKDF2 contra vetores públicos (NIST/RFC)
bash tests/perm/run.sh     # permissões, contas, migração, passwd corrompido, sessão
```

O segundo usa o `vfs.c`, `perm.c`, `user.c` e `sha256.c` **reais** com disco,
memória e relógio simulados. Compila só esses arquivos, em poucos segundos.

## Mover, renomear e copiar

* **`mv` / `rename()`** exige **W+X** na pasta de origem (com a regra *sticky* de `/tmp`) e **W+X**
  na pasta de destino. Nunca sobrescreve; recusa mover uma pasta para dentro de si mesma.
* **`cp`** exige **R** em cada arquivo, **R+X** em cada pasta copiada e **W+X** no destino.
  A cópia passa a pertencer a quem copiou e mantém os bits de modo (copiar um `0600` continua `0600`).
* Ambos gravam o disco **uma vez** só (journal do HAOSFS), mesmo copiando uma árvore inteira.

## Limitações conhecidas

* **Configurações são globais:** tema, idioma e wallpaper ainda ficam em `/etc`
  (compartilhados por todas as contas).
* **Sem grupos compartilhados** nem ACLs.
* O **cwd é global** (um por sistema), não por terminal.
* O bloqueio de login é **só em memória** (reiniciar zera): 5 falhas seguidas travam a conta por
  30 s, dobrando até 15 min.
* O salt/RNG usa RDSEED/RDRAND quando a CPU tem, mais jitter do TSC, num DRBG de SHA-256
  (`kernel/random.c`); sem RNG de hardware a entropia é só a de temporização.
* O sync é **atômico** (journal), mas só ao nível do volume inteiro — não há `fsync` por arquivo.
