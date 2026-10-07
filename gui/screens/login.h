#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Roda a tela de login e só retorna quando alguém efetivamente "entrou":
//   - primeiro boot: cria a primeira conta (administradora);
//   - uma conta: pede a senha, se houver; várias: pergunta o nome antes.
// Autentica contra o banco de usuários do kernel (kernel/user.h,
// /etc/passwd: senhas com salt + PBKDF2-HMAC-SHA256) e, ao final, inicia a
// sessão (session_start): define o usuário logado e o diretório de
// trabalho (a pasta pessoal dele).
void run_login_screen(void);

// Nome do usuário logado, ou "" se ninguém entrou ainda. Atalho para
// session_username() (kernel/user.h). O ponteiro é estável enquanto a
// sessão durar; não deve ser liberado pelo chamador.
const char* login_get_current_username(void);

#ifdef __cplusplus
}
#endif
