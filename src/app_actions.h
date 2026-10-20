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
//     tasti e' una superficie come le altre;
//   * `BuildPaletteMenu`, `BuildGoToMenu` → `view_palette.*` / `view_goto.*`: sono
//     **superfici**, e un comando chiede loro di aprirsi (`commands.*`);
//   * `BuildPinMenu`, `ClosePinMenu` → `view_pins.*`: le due voci di fissaggio hanno
//     gia' un menu loro;
//   * `OpenPropertiesPanel` → `view_properties.*`: il pannello e' una superficie, e
//     le sue righe sono gia' quelle dell'ispettore;
//   * `ComputeLayout` → `layout.*`: la geometria della finestra e' una cosa sola, e
//     non e' un comando;
//   * `BeginOperation` → `view_operation.*`: il pannello dell'operazione in corso;
//   * `ToggleBookmark` → `commands.*`: e' un comando, e ci arrivera' con la prossima
//     passata dei segnalibri (oggi lo chiama `RunCommand` e vive qui).
//
// La regola e' che questa lista **si accorcia**: ogni funzione che trova un modulo
// suo esce da qui, e il file resta con le sole dichiarazioni di cio' che non ha
// ancora una casa. Se la lista ricomincia a crescere, la cucitura e' nel posto
// sbagliato. Il 2026-10-18 il blocco dei **comandi** (`RunCommand`, `ActivateSelection`,
// `CreateNewItem`) e' uscito verso `commands.h`, e la lista si e' ricomposta: sono
// entrate le superfici dei menu, che i comandi devono poter aprire.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <memory>

#include "app_state.h"
#include "file_ops.h"

namespace Bolt
{
// L'editor della mappa dei tasti: costruirlo (col cursore sul comando che si vuole),
// e chiuderlo — che chiude anche la cattura in corso.
void BuildKeyMapMenu(AppState& app, int selectCommand = -1);
void CloseKeyMap(AppState& app);

// Le superfici che un comando apre.
void BuildPaletteMenu(AppState& app);
void BuildGoToMenu(AppState& app);

// Il menu dei fissaggi.
void BuildPinMenu(AppState& app, bool removing);
void ClosePinMenu(AppState& app);

// La geometria della finestra: barre, lista, ispettore. Una sola funzione decide, e
// il disegno e l'input leggono la stessa.
Layout ComputeLayout(const AppState& app);

// Il nostro pannello delle proprieta' (le stesse righe dell'ispettore, piu' grandi).
void OpenPropertiesPanel(AppState& app);

// Il segnalibro della cartella aperta. Un comando: sta qui per la prossima passata.
void ToggleBookmark(AppState& app);

// Un'operazione lunga si avvia sempre da qui: cosi' la sorgente e l'ora non si
// possono dimenticare in uno dei posti che la chiamano, e una seconda operazione
// mentre la prima gira viene rifiutata invece di sostituirla.
bool BeginOperation(AppState& app, std::unique_ptr<FileOps::Job> job, OperationSource source);

// Il controllo degli aggiornamenti. Il lavoro di rete e le impronte stanno in
// `update.*`; qui resta il comando che lo fa partire, e l'esito che si legge quando
// ha finito (→ `view_update.*`, quando ci sara' una superficie da disegnare).
void CheckForUpdates(AppState& app);
} // namespace Bolt
