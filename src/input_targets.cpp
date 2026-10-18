// ---------------------------------------------------------------------------
// Il registro dei bersagli: dove sono le cose che si possono cliccare.
//
// Chi disegna **registra** un rettangolo e un tipo (`ClickTarget`) mentre disegna; chi
// riceve il clic **chiede** qui cosa c'e' sotto il puntatore, invece di ricalcolare le
// stesse geometrie. E' la ragione per cui un clic su una linguetta, su un pulsante o su
// una riga finisce nel posto giusto anche dopo che il layout e' cambiato.
//
// Una funzione sola, e i chiamanti sono una quarantina: il posto e' questo, e non
// `main.cpp`, perche' anche la navigazione chiede dove sono le linguette
// (`UpdateTabDrop`), e un modulo non puo' chiamare una funzione interna a un altro.
// ---------------------------------------------------------------------------
#include "input_targets.h"

namespace Bolt
{
// --- input -----------------------------------------------------------------
const ClickTarget* HitTest(const AppState& app, float x, float y, ClickTarget::Kind kind)
{
    // Reverse order: later targets are drawn on top.
    for (auto it = app.clickTargets.rbegin(); it != app.clickTargets.rend(); ++it)
    {
        if (it->kind == kind && it->rect.Contains(x, y))
            return &*it;
    }
    return nullptr;
}
} // namespace Bolt
