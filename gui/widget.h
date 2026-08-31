#pragma once
#include "../kernel/types.h"
#include "theme.h"

//  widget.h — Widget Core do HAOS
//
//  Substitui o sistema anterior (Window C + Window2/Widget C++
//  coexistindo, cada um com seu próprio conjunto de callbacks
//  fixos: on_click/on_key/on_drag/on_mouse_up espalhados e
//  inconsistentes) por UM único sistema:
//
//    - Widget: classe base para TODO elemento de UI, desde a
//      janela inteira até um botão individual. Usa despacho de
//      evento único (on_event) em vez de callbacks separados.
//    - Container: Widget que tem filhos (janela, painel).
//    - Layout automático (VStack/HStack) — ver layout.h.

// ---- Retângulo simples (preservado do widget.h anterior) ----
struct Rect {
    int32_t  x, y;
    uint32_t w, h;

    bool contains(int32_t px, int32_t py) const {
        return px >= x && px < x + (int32_t)w &&
               py >= y && py < y + (int32_t)h;
    }
};

//  Sistema de eventos unificado
enum class EventType : uint8_t {
    MouseDown,   // botão pressionado dentro do widget
    MouseUp,     // botão solto (widget pode não estar mais sob o cursor)
    MouseMove,   // cursor se movendo (com ou sem botão pressionado)
    MouseDrag,   // movendo COM botão pressionado — equivalente ao
                 // on_drag antigo, usado p.ex. para seleção de texto
    KeyDown,     // tecla pressionada, widget precisa estar focado
    Focus,       // widget acabou de ganhar foco (ex.: via Tab ou clique)
    Blur,        // widget acabou de perder foco
    Resize,      // bounds do widget mudou (ex.: janela redimensionada)
    Paint,       // solicitação explícita de redesenho (raramente usado
                 // diretamente — draw() já cobre a maioria dos casos)
};

struct WidgetEvent {
    EventType type;
    // Coordenadas relativas ao próprio widget (não ao pai) para
    // eventos de mouse — cada widget recebe (0,0) no seu canto
    // superior esquerdo, independente de onde está na tela.
    int32_t   x, y;
    uint8_t   key;       // válido apenas em KeyDown
};

// Resultado do despacho: diz ao chamador (Container/WM) se o evento
// foi tratado (para de propagar) ou deve continuar sendo oferecido
// a outros widgets (ex.: clique fora de qualquer botão específico).
enum class EventResult : uint8_t {
    Ignored,
    Handled,
};

//  Widget — Classe Base Abstrata
class Widget {
public:
    Rect     bounds;     // posição/tamanho relativo ao pai
    bool     visible;
    bool     enabled;
    bool     focused;    // true se este widget tem o foco de teclado

    Widget(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : bounds{x, y, w, h}, visible(true), enabled(true), focused(false) {}

    virtual ~Widget() = default;

    // draw() é puro: cada subclasse concreta DEVE implementar.
    // ox,oy = offset absoluto do pai no framebuffer (acumulado
    // subindo a árvore — cada Container soma sua própria posição
    // antes de desenhar os filhos).
    virtual void draw(int32_t ox, int32_t oy) = 0;

    // Despacho de evento único. Implementação padrão: ignora tudo
    // (widgets "burros" como Label não precisam sobrescrever nada).
    // Widgets interativos sobrescrevem para tratar os tipos que
    // fazem sentido para eles, retornando Handled quando consomem
    // o evento (impede que Container/WM continue propagando).
    virtual EventResult on_event(const WidgetEvent& /*ev*/) {
        return EventResult::Ignored;
    }

    // Tamanho "natural" do widget quando o layout automático (ver
    // layout.h) precisa decidir quanto espaço reservar para ele.
    // Widgets de tamanho fixo (Button, TextField) retornam bounds.w/h;
    // widgets que se ajustam ao conteúdo (Label) recalculam aqui.
    virtual uint32_t preferred_width()  const { return bounds.w; }
    virtual uint32_t preferred_height() const { return bounds.h; }
};

//  Nó interno da lista ligada de Widgets (preservado — já
//  funciona bem e evita depender de stdlib)
struct WidgetNode {
    Widget*     widget;
    WidgetNode* next;
};

//  WidgetList — coleção ordenada de widgets, com despacho de
//  evento percorrendo a lista até algum widget tratar (Handled).
class WidgetList {
public:
    WidgetList() : m_head(nullptr), m_count(0) {}
    ~WidgetList() { clear(); }

    void add(Widget* w);
    void clear();

    void draw_all(int32_t ox, int32_t oy);

    // Percorre os widgets tentando despachar o evento (já com as
    // coordenadas convertidas para o espaço de CADA widget). Para
    // no primeiro que retornar Handled. Retorna o próprio resultado.
    EventResult dispatch(const WidgetEvent& ev, int32_t ox, int32_t oy);

    int count() const { return m_count; }
    WidgetNode* head() const { return m_head; }

private:
    WidgetNode* m_head;
    int         m_count;
};
