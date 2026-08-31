#pragma once
#include "container.h"

// ============================================================
//  layout.h — Layout automático (VStack / HStack)
//
//  Resolve o problema de posicionar manualmente cada widget com
//  x/y absolutos — especialmente doloroso em telas de formulário
//  (o caso de uso principal que você descreveu: configurações,
//  editores). Em vez disso:
//
//    VStack* form = new VStack(x, y, w);
//    form->add(new Label(0, 0, "Nome:"));
//    form->add(new TextField(0, 0, 200, 26));
//    form->add(new Checkbox(0, 0, "Lembrar"));
//    form->add(button_salvar);
//
//  Cada add() reposiciona automaticamente o widget logo abaixo do
//  anterior, com o espaçamento definido pelo tema atual
//  (theme->spacing_md). Os x/y passados ao construir cada widget
//  filho são ignorados pelo layout (o VStack os sobrescreve) —
//  você só precisa se preocupar com w/h.
//
//  HStack funciona igual, mas empilha da esquerda para a direita.
//
//  Ambos herdam de Container, então ainda têm despacho de evento
//  e desenho de filhos "de graça" — um VStack/HStack é, na prática,
//  um Container que se auto-organiza toda vez que um filho é
//  adicionado.
// ============================================================

class VStack : public Container {
public:
    // w: largura fixa do stack (0 = usa a largura do filho mais largo,
    //    recalculada a cada add()).
    VStack(int32_t x, int32_t y, uint32_t w, uint32_t spacing_override = 0)
        : Container(x, y, w, 0), m_spacing_override(spacing_override) {}

    // Adiciona um filho, reposicionando-o automaticamente abaixo do
    // último. Sobrescreve Container::add (não é virtual — decisão
    // deliberada: VStack não é usado polimorficamente como Container
    // genérico nos pontos onde chamamos add(), então o "shadowing"
    // aqui é seguro e evita o custo/RTTI de tornar add() virtual só
    // por causa disso).
    void add(Widget* child);

private:
    uint32_t m_spacing_override; // 0 = usa theme_current()->spacing_md
    uint32_t spacing() const;
};

class HStack : public Container {
public:
    HStack(int32_t x, int32_t y, uint32_t h, uint32_t spacing_override = 0)
        : Container(x, y, 0, h), m_spacing_override(spacing_override) {}

    void add(Widget* child);

private:
    uint32_t m_spacing_override;
    uint32_t spacing() const;
};
