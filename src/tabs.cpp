#include "tabs.h"

#include <functional>

namespace Bolt
{
int TabById(const AppState& app, int id)
{
    for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i)
    {
        if (app.tabs[static_cast<size_t>(i)].id == id)
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
        const int parent = app.tabs[static_cast<size_t>(index)].parentId;
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

std::vector<std::pair<int, int>> TabOrder(const AppState& app)
{
    std::vector<std::pair<int, int>> out;
    out.reserve(app.tabs.size());

    // I figli si mettono subito dopo il genitore, ricorsivamente: un ramo alla
    // volta, nell'ordine di nascita. Le radici in fondo.
    std::function<void(int, int)> walk = [&](int parentId, int depth)
    {
        for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i)
        {
            if (app.tabs[static_cast<size_t>(i)].parentId != parentId)
                continue;
            out.push_back({i, depth});
            walk(app.tabs[static_cast<size_t>(i)].id, depth + 1);
        }
    };
    walk(-1, 0);
    return out;
}


} // namespace Bolt
