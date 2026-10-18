// ---------------------------------------------------------------------------
// Il pannello delle opzioni, visto da fuori.
//
// Sette funzioni, quelle che `main.cpp` chiama davvero: costruire l'elenco,
// applicare una voce, i tasti e i clic del pannello, e le due code della modifica
// in linea (confermare e annullare). Tutto il resto — i controlli, i valori
// tipizzati, la scelta — e' chiuso dentro `view_options.cpp`.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include "app_state.h"
#include "view_menu.h"

namespace Bolt
{
// Ricostruisce l'elenco delle preferenze, leggendole da `AppState`.
void BuildOptionsMenu(AppState& app);

// Porta nella riga il controllo giusto per l'opzione che rappresenta, e applica
// all'`AppState` la scelta fatta su quella riga. Sono le due direzioni della
// stessa verita': `AppState` e' l'unica copia dello stato.
void ApplyOptionFromItem(AppState& app, GroupMenuItem& item);

// I tasti del pannello: filtro, modifica in linea, scelta. Falso se il tasto non
// riguarda il pannello e deve proseguire.
bool OptionsKeyDown(AppState& app, GroupMenu& menu, WPARAM key);

// Un clic dentro il pannello. Restituisce cosa va fatto fuori dal pannello (per
// esempio aprire l'editor della mappa dei tasti), come gli altri menu a gruppi.
MenuAction OptionsClick(AppState& app, const Layout& layout, GroupMenu& menu, float x, float y);

// La modifica in linea di un valore scritto a mano: si annulla o si conferma.
void CancelOptionEdit(AppState& app);
void CommitOptionEdit(AppState& app);

// Un clic su una voce della lista di scelta (lingua, tavolozza, misura del
// carattere): applica e chiude.
void RunOptionsChoiceSelection(AppState& app);
} // namespace Bolt
