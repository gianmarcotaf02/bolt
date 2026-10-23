#include "tabs.h"

#include <functional>

namespace Bolt
{
namespace
{
// L'albero di **un** pannello. Prima erano una fila sola, in cima alla finestra, e
// bastava quella attiva; adesso ogni pannello disegna la sua sopra il suo riquadro, e
// chi legge l'albero deve dire **quale**: un indice preso dalla fila sbagliata sposta
// un tab che non c'entra. `-1` vuol dire "quello attivo", che e' la domanda giusta per
// chi sta eseguendo un gesto dell'utente.
const std::vector<Tab>& Tabs(const AppState& app, int pane)
{
    const int index = (pane >= 0 && pane < static_cast<int>(app.panes.size())) ? pane
                                                                              : app.activePane;
    return app.panes[static_cast<size_t>(index)].tabs;
}
} // namespace

int TabById(const AppState& app, int id, int pane)
{
    const std::vector<Tab>& tabs = Tabs(app, pane);
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
    {
        if (tabs[static_cast<size_t>(i)].id == id)
            return i;
    }
    return -1;
}

int TabRootId(const AppState& app, int id)
{
    // Il ciclo si ferma anche se qualcuno, un giorno, chiudesse un anello: un
    // albero sbagliato deve disegnarsi male, non bloccare la finestra.
    for (int guard = 0; guard < 64; ++guard)
    {
        const int index = TabById(app, id);
        if (index < 0)
            return id;
        const int parent = Tabs(app, -1)[static_cast<size_t>(index)].parentId;
        if (parent < 0)
            return id;
        id = parent;
    }
    return id;
}

bool TabIsDescendantOf(const AppState& app, int id, int ancestorId)
{
    if (id == ancestorId)
        return true;
    return TabRootId(app, id) == ancestorId;
}

std::vector<std::pair<int, int>> TabOrder(const AppState& app, int pane)
{
    const std::vector<Tab>& tabs = Tabs(app, pane);
    std::vector<std::pair<int, int>> out;
    out.reserve(tabs.size());

    // I figli si mettono subito dopo il genitore, ricorsivamente: un ramo alla
    // volta, nell'ordine di nascita. Le radici in fondo.
    std::function<void(int, int)> walk = [&](int parentId, int depth)
    {
        for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
        {
            if (tabs[static_cast<size_t>(i)].parentId != parentId)
                continue;
            out.push_back({i, depth});
            walk(tabs[static_cast<size_t>(i)].id, depth + 1);
        }
    };
    walk(-1, 0);
    return out;
}


} // namespace Bolt
