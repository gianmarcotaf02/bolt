// ---------------------------------------------------------------------------
// La finestra dell'aggiornamento: il lavoro e il suo esito, in un posto solo.
//
// E' **modale** come il pannello delle proprieta': finche' e' aperta, clic, rotella e
// tasti si fermano li'. Dentro ci succede tutto — il controllo, il download con la
// barra, la verifica dell'impronta, l'esito — e quando il setup e' pronto la finestra
// **chiede** di riavviare invece di chiudere il programma da sola, che e' quello che
// fino al 2026-10-23 faceva sembrare un aggiornamento un guasto.
//
// Il lavoro vero non e' qui: e' `Update::Job`, su un thread (`update.*`). Qui si legge
// `Progress()` a ogni fotogramma e si chiama le azioni che toccano la finestra.
// `docs/04` M13, «La finestra dell'aggiornamento».
// ---------------------------------------------------------------------------
#pragma once

#include <string>

#include <windows.h>

#include "app_state.h"
#include "update.h"

namespace Bolt
{
// Vero se la finestra e' aperta: chi riceve un clic o un tasto la guarda per prima.
bool UpdateDialogVisible(const AppState& app);

// La riga che racconta a che punto e' il lavoro. Sta qui e non in `main.cpp` perche'
// la finestra e' l'unico posto che la mostra.
std::wstring UpdateProgressLine(const Update::Snapshot& snapshot);

// La finestra, e i due pulsanti in fondo. Il primario e' quello che conta — `Annulla`
// mentre si scarica, `Riavvia ora` quando il setup e' pronto, `Chiudi` per un esito —
// e il secondario esiste solo quando c'e' una scelta da fare (`Piu' tardi`).
Rect UpdateDialogRect(const AppState& app, const Layout& layout);
Rect UpdateDialogPrimaryButton(const AppState& app, const Layout& layout, const Rect& dialog);
Rect UpdateDialogSecondaryButton(const AppState& app, const Layout& layout, const Rect& dialog);

void DrawUpdateDialog(AppState& app, const Layout& layout);

// Il clic, ovunque cada nella finestra: fuori dai pulsanti non fa niente, perche' una
// finestra modale non si chiude dove capita.
void UpdateDialogClick(AppState& app, float x, float y);

// `Esc` annulla il download in corso e chiude la finestra quando il lavoro e' finito;
// `Invio` preme il pulsante primario.
void UpdateDialogKey(AppState& app, WPARAM key);

void UpdateDialogPrimary(AppState& app);
void UpdateDialogSecondary(AppState& app);
} // namespace Bolt
