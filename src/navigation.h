// ---------------------------------------------------------------------------
// La navigazione e i tab, visti da fuori.
//
// Aprire una cartella (che e' anche leggere le voci e aggiornare la scheda), muoversi
// avanti e indietro, e l'albero dei tab: aprirne uno, attivarlo, chiuderlo, chiudere un
// ramo, e accogliere un tab trascinato sopra un altro.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// Legge una cartella e la mostra. |recordHistory| falso quando si sta ricaricando la
// stessa cartella: ricaricare non e' navigare, e la storia non deve crescere.
void LoadDirectory(AppState& app, const std::wstring& directory, bool recordHistory = true);

// Ricarica la cartella tenendo il posto: selezione, cursore e scorrimento restano dove
// l'utente li ha lasciati.
void ReloadKeeping(AppState& app, const std::unordered_set<std::wstring>& keep,
                   const std::wstring& preferCursor, float restoreScroll);

// Le voci di una cartella, cosi' come le legge la lista. Sono qui perche' chi naviga le
// usa, e chi le prova le chiama da qui.
// `longPaths`: il prefisso Win32 davanti al percorso, che serve a `FindFirstFile`
// quando la cartella sta oltre i 260 caratteri (`LongPathsSupport`).
std::vector<Entry> ReadDirectory(const std::wstring& directory, std::string* error,
                                 bool longPaths);
std::vector<Entry> ReadNamespaceDirectory(AppState& app, const std::wstring& location);

// La scheda nel titolo della finestra.
void UpdateWindowTitle(AppState& app);

// Il tab e la sua pila di cartelle.
void SaveActiveTab(AppState& app);
void LoadActiveTab(AppState& app);

// Lo **scambio puro** con il pannello |pane|: i campi del suo tab attivo e la vista
// che ne deriva passano in vita, e quelli vivi vanno li'. Non aggiorna il titolo, non
// annulla code di lavoro, non alza `viewDirty`: chi lo chiama non sta cambiando
// pannello, sta solo **guardando** l'altro. Chiamarlo due volte riporta tutto com'e' —
// e' la sua propria inversa — e chi disegna un pannello inattivo lo chiama due volte
// per fotogramma: per questo e' un `swap` di campi, non una copia.
void SwapLiveWithPane(AppState& app, int pane);

// Il pannello che prende il fuoco: lo stato vivo va nel pannello che si lascia, quello
// del pannello nuovo viene in vita. Non e' `SwapLiveWithPane`: quello scambia con un
// pannello e basta, e chi cambia pannello ne ha due da mettere a posto.
void ActivatePane(AppState& app, int pane);

// `Dividi a destra` / `Dividi in basso`: il secondo pannello, sulla stessa cartella.
// Con due pannelli gia' aperti cambia solo l'asse — chiudere un pannello e' un'altra
// tappa, e un comando che non fa quello che dice e' peggio di un comando che manca.
void SplitPane(AppState& app, bool column);
// Un tab nuovo: si apre sulla cartella data, o su quella aperta se e' vuota.
void NewTab(AppState& app, const std::wstring& directory = std::wstring());
void ActivateTab(AppState& app, int index);
void CloseTab(AppState& app, int index);
// Chiude il ramo di |id| (il tab e i suoi figli); con |wholeTree| chiude la radice.
void CloseTabTree(AppState& app, int id, bool wholeTree);

// Il trascinamento di un tab: dove finirebbe, e cosa succede al rilascio.
void UpdateTabDrop(AppState& app, float x, float y);
void ApplyTabDrop(AppState& app);

// Avanti e indietro nella storia della cartella.
void NavigateBack(AppState& app);
void NavigateForward(AppState& app);
} // namespace Bolt
