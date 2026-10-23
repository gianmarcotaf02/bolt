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

// Il menu dell'applicazione, dietro il pulsante ☰: e' una **superficie**, e come le
// altre sta qui finche' non ha un modulo suo (`view_app_menu.*`).
void BuildAppMenu(AppState& app, float x, float y);
void ClosePinMenu(AppState& app);

// La geometria della finestra: barre, lista, ispettore. Una sola funzione decide, e
// il disegno e l'input leggono la stessa. Con due pannelli e' il layout di quello
// attivo, e chi la chiama non deve sapere che l'altro esista.
Layout ComputeLayout(const AppState& app);

// Le tre funzioni da cui `ComputeLayout` e' fatta, per i due pannelli. Con un pannello
// solo `ComputeLayout` e `WindowLayout` sono la stessa cosa, ed e' quello che permette
// a hit test e comandi di continuare a leggere un layout solo.
Layout WindowLayout(const AppState& app);          // la finestra intera
Layout PaneLayout(const AppState& app, int pane);   // un pannello: intestazione, filtro, lista
Rect   SplitterRect(const AppState& app);           // la striscia fra i due pannelli (6 px)

// La striscia fra i due pannelli: sei pixel. Un divisore di un pixel non si prende col
// mouse, e il bordo resta invisibile finche' non ci si passa sopra. Qui e non in
// `main.cpp` perche' lo leggono anche l'input del mouse (il verso del cursore, il
// trascinamento) e la geometria: un numero solo, in un posto solo.
constexpr float kSplitterWidth = 6.0f;

// Il nostro pannello delle proprieta' (le stesse righe dell'ispettore, piu' grandi).
void OpenPropertiesPanel(AppState& app);

// Il segnalibro della cartella aperta. Un comando: sta qui per la prossima passata.
void ToggleBookmark(AppState& app);

// Un'operazione lunga si avvia sempre da qui: cosi' la sorgente e l'ora non si
// possono dimenticare in uno dei posti che la chiamano. Accettata vuol dire **sara'
// fatta**: se ce n'e' gia' una in corso la richiesta va in coda, e parte quando quella
// finisce.

// L'installazione di un aggiornamento gia' verificato: si avvia il setup, Bolt si
// chiude, e alla fine Bolt si riapre dalla copia installata (`docs/04` M13). Vive in
// `main.cpp` perche' lancia un processo e chiude la finestra — non e' una superficie.
void BeginInstallUpdate(AppState& app);
bool BeginOperation(AppState& app, FileOps::Request request, OperationSource source);

// Fa avanzare la coda: chiamata quando l'operazione in corso ha finito. Sta qui
// accanto a `BeginOperation` perche' e' l'altra meta' della stessa regola — una
// richiesta accettata e' una richiesta che parte.
void StartNextOperation(AppState& app);

// Il controllo degli aggiornamenti. Il lavoro di rete e le impronte stanno in
// `update.*`; qui resta il comando che lo fa partire, e l'esito che si legge quando
// ha finito (→ `view_update.*`, quando ci sara' una superficie da disegnare).
void CheckForUpdates(AppState& app);

// --- le superfici che l'input chiama -----------------------------------------
//
// Uscite dal namespace anonimo di `main.cpp` il 2026-10-23, quando i `case` di
// `WindowProc` sono diventati `input_keys.cpp` e `input_mouse.cpp`: il corpo di un
// `case` e' l'azione della superficie che riceve l'evento, quindi l'evento non
// poteva uscire senza che la superficie avesse un nome. La destinazione e' scritta
// accanto a ognuna: sono superfici, e ognuna avra' il suo modulo.

// Il campo di ricerca: `Ctrl+F` lo accende, i caratteri lo riempiono a passi.
void StartSearch(AppState& app, std::wstring text);
void StopSearch(AppState& app);

// Le colonne: dove cade un punto dell'intestazione e il menu del tasto destro che
// ci sta sopra. Destinazione `view_columns.*`.
int ColumnAtPoint(const AppState& app, const Layout& layout, float x);
void BuildColumnOpsMenu(AppState& app, int column);
void BuildColumnMenu(AppState& app, int column);
void RunColumnMenuSelection(AppState& app);
void RunColumnOpsSelection(AppState& app);

// Il pannello delle proprieta': la finestra nostra, modale. Destinazione
// `view_properties.*`.
Rect PropertiesPanelRect(const AppState& app, const Layout& layout);
Rect PropertiesWindowsButton(const AppState& app, const Layout& layout, const Rect& panel);
bool PropertiesPanelKey(AppState& app, WPARAM key);

// L'editor della mappa dei tasti: la cattura di un tasto, la navigazione
// dell'elenco, la scelta. Destinazione `view_keymap.*`.
void BeginKeyCapture(AppState& app, int command);
void KeyMapKeyDown(AppState& app, WPARAM key);
void RunKeyMapSelection(AppState& app);

// Il menu dei fissaggi. Destinazione `view_pins.*`.
void RunPinMenuSelection(AppState& app);

// Il pannello dell'operazione in corso: dove sta, il pulsante, l'annullamento.
// Destinazione `view_operation.*`.
bool OperationPanelVisible(const AppState& app);
Rect OperationPanelRect(const AppState& app, const Layout& layout);
Rect OperationCancelButton(const AppState& app, const Layout& layout, const Rect& panel);
void CancelOperation(AppState& app);

// La scelta dentro la palette e dentro il GoTo. Destinazione `view_palette.*` e
// `view_goto.*`.
void RunPaletteSelection(AppState& app);
void RunGoToSelection(AppState& app);
} // namespace Bolt
