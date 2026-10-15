// ---------------------------------------------------------------------------
// La barra del titolo: la striscia dei tab, il menu, i pulsanti di finestra.
//
// E' il secondo pezzo di **vista** uscito da `main.cpp`. Qui sta anche `TabOrder`,
// che e' la forma della striscia — l'albero dei tab appiattito nell'ordine in cui
// si legge — e non il modello dei tab, che resta dov'e' (chi ne crea, chiude e
// attiva e' l'input, non il disegno).
//
// La **geometria** dei pulsanti di finestra non esce da qui: chi riceve i clic la
// legge dai bersagli che questo file registra mentre disegna
// (`ClickTarget::Kind::WindowMinimize` e compagni), invece di ricalcolarla. Due
// calcoli della stessa cosa divergono sempre, e qui il secondo sarebbe stato
// invisibile: un pulsante che si vede e non si clicca.
// ---------------------------------------------------------------------------
#pragma once

#include "app_state.h"
#include "tabs.h"

namespace Bolt
{
// Disegna la striscia dei tab, il pulsante del menu e i tre pulsanti di finestra,
// e registra i loro bersagli per i clic.
void DrawTitleBar(AppState& app, const Layout& layout);
} // namespace Bolt
