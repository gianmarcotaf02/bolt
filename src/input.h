// ---------------------------------------------------------------------------
// L'input della finestra: i messaggi della tastiera e quelli del mouse.
//
// Uscito da `main.cpp` il 2026-10-23. `docs/05` §2 aveva deciso l'ordine: prima le
// superfici (fatte, una per volta, fino ai due pannelli), **poi gli eventi** —
// perche' il corpo di un `case` e' l'azione della superficie che riceve l'evento, e
// finche' la superficie viveva nello stesso file il `case` non poteva uscirne senza
// trascinarsela dietro. Le superfici sono fuori tutte, quindi i `case` sono usciti
// **di peso**: lo `switch` che li conteneva e' qui, in una funzione per famiglia di
// messaggi, e il testo dei casi non e' cambiato di una riga.
//
// `main.cpp` resta la finestra: `wWinMain`, la classe, i messaggi che riguardano la
// finestra e basta (`WM_NCCALCSIZE`, `WM_NCHITTEST`, `WM_SIZE`,
// `WM_SETTINGCHANGE`, `WM_DESTROY`) e lo smistamento.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include "app_state.h"

namespace Bolt
{
// Il valore che un handler restituisce quando il messaggio non e' suo. Non e' una
// risposta che queste funzioni diano mai per scelta: le risposte vere sono 0, TRUE
// o FALSE, e nessun messaggio di questa famiglia si aspetta un numero negativo.
// Un `break` che nel vecchio `WindowProc` usciva dallo `switch` e arrivava a
// `DefWindowProcW` adesso esce da questo `switch` e arriva qui: la stessa cosa,
// detta al chiamante invece che al sistema.
constexpr LRESULT kMessageNotHandled = -1;

// I tasti: `WM_KEYDOWN`, `WM_CHAR`, `WM_SYSKEYDOWN`, `WM_SYSCHAR`.
LRESULT HandleKeyMessage(HWND window, AppState* app, UINT message, WPARAM wparam, LPARAM lparam);

// Il mouse: rotella, movimento, i tre pulsanti, il cursore.
LRESULT HandleMouseMessage(HWND window, AppState* app, UINT message, WPARAM wparam, LPARAM lparam);
} // namespace Bolt
