#include "layout.h"

//  VStack
uint32_t VStack::spacing() const {
    return m_spacing_override ? m_spacing_override : theme_current()->spacing_md;
}

void VStack::add(Widget* child) {
    if (!child) return;

    // Posiciona o novo filho logo abaixo do último já adicionado.
    // bounds.h do próprio VStack funciona como "próxima posição Y
    // livre" — cresce a cada add(), então usamos ela diretamente
    // como coordenada Y relativa do próximo filho.
    int32_t next_y = (int32_t)bounds.h;
    if (m_children.count() > 0) {
        next_y += (int32_t)spacing();
    }

    child->bounds.x = 0; // VStack sempre alinha filhos à esquerda
    child->bounds.y = next_y;

    // Se o widget não tem largura própria definida (w == 0, caso de
    // Label antes de set_text, ou widget recém-criado), usa a
    // preferida; senão respeita a largura já configurada pelo
    // chamador (ex.: TextField criado com w=200 explícito).
    if (child->bounds.w == 0) {
        child->bounds.w = child->preferred_width();
    }

    uint32_t child_h = child->bounds.h ? child->bounds.h : child->preferred_height();
    child->bounds.h = child_h;

    Container::add(child);

    // Atualiza a altura total do VStack para incluir o novo filho —
    // é isso que faz o PRÓXIMO add() saber onde continuar empilhando.
    bounds.h = (uint32_t)(next_y + (int32_t)child_h);

    // Se a largura não foi fixada no construtor (w=0), cresce para
    // acomodar o filho mais largo — comportamento tipo "encolhe
    // para caber o conteúdo" quando não há largura fixa desejada.
    if (child->bounds.w > bounds.w) {
        bounds.w = child->bounds.w;
    }
}

//  HStack

uint32_t HStack::spacing() const {
    return m_spacing_override ? m_spacing_override : theme_current()->spacing_md;
}

void HStack::add(Widget* child) {
    if (!child) return;

    int32_t next_x = (int32_t)bounds.w;
    if (m_children.count() > 0) {
        next_x += (int32_t)spacing();
    }

    child->bounds.y = 0; // HStack sempre alinha filhos ao topo
    child->bounds.x = next_x;

    if (child->bounds.w == 0) {
        child->bounds.w = child->preferred_width();
    }
    uint32_t child_h = child->bounds.h ? child->bounds.h : child->preferred_height();
    child->bounds.h = child_h;

    Container::add(child);

    bounds.w = (uint32_t)(next_x + (int32_t)child->bounds.w);
    if (child_h > bounds.h) {
        bounds.h = child_h;
    }
}
