// ---------------------------------------------------------------------------
// La cucitura dello spezzamento: le azioni che vivono ancora in `main.cpp`.
//
// Questo header esiste per una ragione sola, ed e' una ragione temporanea: quando
// un modulo ha bisogno di una funzione che sta ancora nel file unico, la scelta non
// e' fra "chiamarla" e "non chiamarla" ma fra **dichiararla** e lasciarla interna,
// cioe' fra un confine scritto e uno implicito. Qui sta l'elenco, con la sua
// destinazione accanto:
//
//   * `BuildKeyMapMenu`, `CloseKeyMap` → `view_keymap.*`: l'editor della mappa dei
//     tasti e' una superficie come le altre, e sara' la prossima a uscire;
//   * `ClosePinMenu` → `view_pins.*`: le due voci di fissaggio hanno gia' un menu
//     loro (`BuildPinMenu`), e chiuderlo e' la sola cosa che serve da fuori.
//
// La regola e' che questa lista **si accorcia**: ogni funzione che trova un modulo
// suo esce da qui, e il file resta con le sole dichiarazioni di cio' che non ha
// ancora una casa. Se la lista ricomincia a crescere, la cucitura e' nel posto
// sbagliato.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include "app_state.h"

namespace Bolt
{
// L'editor della mappa dei tasti: costruirlo (col cursore sul comando che si vuole),
// e chiuderlo — che chiude anche la cattura in corso.
void BuildKeyMapMenu(AppState& app, int selectCommand = -1);
void CloseKeyMap(AppState& app);

// Il menu dei fissaggi.
void ClosePinMenu(AppState& app);

// --- le azioni che il menu contestuale chiama -------------------------------
//
// Sono i **comandi**: il menu e' una superficie e chiede a loro di fare. Questo e' il
// blocco che diventera' `src/commands.*` — `RunCommand` e i `Do*` — e quando ci sara'
// queste dichiarazioni escono di qui, perche' il modulo dei comandi avra' il suo
// header. Sono qui adesso perche' l'alternativa era lasciare il menu dentro
// `main.cpp`, dove i `case` di `WindowProc` non si possono spostare.

// Esegue un comando della tabella: e' il punto in cui un tasto, una voce di un menu o
// una riga della palette diventano la stessa cosa.
void RunCommand(AppState& app, int command);

// Apre la voce scelta (cartella, file, spazio dei nomi).
void ActivateSelection(AppState& app);

// La geometria della finestra: barre, lista, ispettore. Una sola funzione decide, e
// il disegno e l'input leggono la stessa.
Layout ComputeLayout(const AppState& app);

// Il nostro pannello delle proprieta' (le stesse righe dell'ispettore, piu' grandi).
void OpenPropertiesPanel(AppState& app);

// Crea una voce dal modello |index| di `ShellNew`: file nuovo nella cartella aperta.
void CreateNewItem(AppState& app, size_t index);
} // namespace Bolt
