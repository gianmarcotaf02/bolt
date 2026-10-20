// ---------------------------------------------------------------------------
// I comandi: quello che succede quando qualcuno **chiede** qualcosa.
//
// Un tasto, una voce di un menu, una riga della palette e un clic del transcorso
// finiscono tutti qui: `RunCommand` e' il punto in cui un gesto diventa un effetto,
// e i `Do*` sono gli effetti che agiscono sui file.
//
// Usciti da `main.cpp` il 2026-10-18, ultimo blocco dello spezzamento che era anche
// il primo della lista: **M4 passa di qui** (`Cancella recenti`, i menu dei recenti),
// e spostare un pezzo mentre ci si lavora costa meno che spostarlo dopo.
//
// Cosa **non** c'e', e perche': le superfici dei menu (`BuildPaletteMenu`,
// `BuildGoToMenu`, `BuildPinMenu`, `BuildKeyMapMenu`) e il pannello delle proprieta'
// sono ancora in `main.cpp`, e questo modulo li chiama attraverso
// `app_actions.h` — la cucitura dichiarata. Un comando chiede a una superficie di
// aprirsi, non la disegna.
// ---------------------------------------------------------------------------
#pragma once

#include <cstddef>

#include "app_state.h"

namespace Bolt
{
// Esegue un comando della tabella: e' il punto in cui un tasto, una voce di un menu o
// una riga della palette diventano la stessa cosa.
void RunCommand(AppState& app, int command);

// Apre la voce scelta (cartella, file, spazio dei nomi).
void ActivateSelection(AppState& app);

// Crea una voce dal modello |index| di `ShellNew`: file nuovo nella cartella aperta.
void CreateNewItem(AppState& app, size_t index);

// La rinomina in linea: la comincia il cursore, la chiudono Invio e Esc — che sono
// messaggi della finestra, non comandi.
void BeginRename(AppState& app);
void CancelRename(AppState& app);
void CommitRename(AppState& app);
} // namespace Bolt
