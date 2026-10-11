#pragma once

// Lança um programa .hexec SEM terminal: é o que o Explorador usa para abrir
// um arquivo no aplicativo associado (ex.: imagem -> visualizador). O processo
// roda com a identidade do usuário logado; sua saída de texto (stdout) é
// descartada — programas daqui devem mostrar tudo numa janela própria.
//
//   app_launch("/bin/imgview.hexec", "/home/ana/foto.png");
//
// `arg1` pode ser nullptr. Devolve true se o processo foi criado (o programa
// ainda precisa conseguir abrir a janela dele).
bool app_launch(const char* hexec_path, const char* arg1);
