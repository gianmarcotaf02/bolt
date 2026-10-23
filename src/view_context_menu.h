// ---------------------------------------------------------------------------
// Il menu contestuale, visto da fuori.
//
// Due domande e un'azione: cosa c'e' sotto il puntatore (`ContextMenuClick`,
// `ContextMenuHover`, `ContextMenuDescend`), come si disegna (`ContextMenuDraw`), e
// cosa succede quando si sceglie (`ContextMenuActivate`). Piu' la vita del menu:
// aprirlo su una posizione (`BuildContextMenu`), chiuderlo (`ContextMenuClose`),
// spostare il cursore e tenerlo dentro la finestra.
//
// Chi chiama non sa com'e' fatto un livello: lo chiede qui (`ActiveDepth`).
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// Quanti livelli sono aperti: 1 e' il menu, 2 il sottomenu, e cosi' via.
int ActiveDepth(const ContextMenu& menu);

// Apre il menu alla posizione indicata, riempiendolo con i comandi nostri e con le
// voci della shell. Chiude quello che era aperto.
void BuildContextMenu(AppState& app, float x, float y);

// Il menu di una riga della barra laterale: le voci che agiscono su **quel**
// percorso (fissarlo, toglierlo, toglierlo dai recenti), non sulla cartella aperta.
void BuildSidebarContextMenu(AppState& app, float x, float y, const ClickTarget& row);

// Chiude tutto il menu (e i sottomenu), staccando gli oggetti COM senza liberarli:
// restano nella cache per la prossima apertura.
void ContextMenuClose(AppState& app);

// Tiene il menu dentro la finestra: un menu che esce dal bordo non si puo' leggere
// tutto, e "segui la selezione" lo sposta quando quella cambia.
void ContextMenuClampWindow(AppState& app, const Layout& layout, int depth,
                           bool followSelection = true);

// Il cursore dentro il menu: su e giu' fra le voci, a inizio e fine, e dentro un
// sottomenu quando ce n'e' uno aperto.
void ContextMenuMove(AppState& app, const Layout& layout, int delta);
void ContextMenuJump(AppState& app, const Layout& layout, bool toStart);
void ContextMenuDescend(AppState& app, const Layout& layout);
void ContextMenuRaiseLevel(AppState& app);

// La digitazione e il `Backspace` nel menu aperto: il campo di ricerca in cima al
// livello 0 si riempie come quello della palette, e le voci che non combaciano
// spariscono. Non sono tasti del menu come le frecce — sono **testo**, e il menu li
// prende solo perche' finche' e' aperto e' lui a ricevere la tastiera.
void ContextMenuChar(AppState& app, wchar_t character);
void ContextMenuBackspace(AppState& app);

// Il puntatore e il clic: vero se il menu ha consumato l'evento.
bool ContextMenuHover(AppState& app, const Layout& layout, float x, float y);
bool ContextMenuClick(AppState& app, const Layout& layout, float x, float y);

// Esegue la voce scelta: un nostro comando, un verbo della shell, o un tipo di
// "Nuovo". Le voci che non si possono eseguire non arrivano qui.
void ContextMenuActivate(AppState& app);

// Il disegno del menu e di tutti i suoi livelli.
void ContextMenuDraw(AppState& app, const Layout& layout);

// La finestra delle proprieta' di Windows, per i percorsi dati. Zero se l'utente
// l'ha chiusa: serve a chi lo chiama per decidere se mostrare le nostre.
int ShowProperties(HWND owner, const std::vector<std::wstring>& paths);

// Il menu contestuale della riga del cursore: e' quello che aprono il tasto Menu e
// `Shift+F10`, e deve cadere dove cade il tasto destro sul file scelto.
void OpenContextMenuAtCursor(AppState& app, const Layout& layout);
} // namespace Bolt
