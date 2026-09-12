#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Roda a tela de login e só retorna quando o usuário efetivamente
// "entrou" (nome configurado no primeiro boot, e senha validada se
// houver uma configurada). Substitui a antiga draw_welcome_screen(),
// que era só decorativa (sem interação real, sem estado). Grava/lê
// o nome do usuário (e hash de senha opcional) em /etc/user.cfg via
// VFS, então persiste entre reboots assim que o disco é salvo
// (haosfs_save).
void run_login_screen(void);

// Retorna o nome do usuário atualmente configurado em /etc/user.cfg,
// ou uma string vazia "" se ainda não houver usuário configurado
// (não deveria acontecer depois de run_login_screen ter rodado uma
// vez, mas é seguro chamar antes também). O ponteiro retornado
// aponta para um buffer estático interno — válido até a próxima
// chamada, não precisa (nem deve) ser liberado pelo chamador.
const char* login_get_current_username(void);

#ifdef __cplusplus
}
#endif
