// ---------------------------------------------------------------------------
// I menu: il componente a gruppi (palette, GoTo, opzioni, valori di una colonna).
//
// Era in `main.cpp`, dove il componente stava in mezzo ai comandi che esegue. Qui
// c'e' il **widget**: lo stato, la geometria, il disegno e la tastiera. Non c'e' cosa
// significa una voce scelta — un comando, un percorso, un'opzione — perche' quello lo
// sa chi ha costruito il menu: `GroupMenuClick` e `GroupMenuKeyDown` restituiscono un
// `MenuAction`, e chi li chiama lo esegue.
//
// La separazione e' quella della lista: il disegno sa **dove sono le righe**, l'input
// sa cosa fanno. E il componente era gia' scritto cosi' — la funzione che riceve il
// clic non esegue niente — quindi si e' potuto spostare senza toccarne la logica.
//
// Restano in `main.cpp`, e non sono il widget: cosa va **dentro** un menu (la
// tavolozza dei comandi, le opzioni, i livelli del menu contestuale coi verbi della
// shell) e cosa fa una voce scelta.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// --- il componente: stato, filtro, selezione ---------------------------------
void       GroupMenuOpen(GroupMenu& menu, std::string title, std::string placeholder,
                         std::vector<GroupMenuItem> items);
void       GroupMenuRebuild(GroupMenu& menu);
void       GroupMenuMove(GroupMenu& menu, int delta);
int        GroupMenuSelectedId(const GroupMenu& menu);

// Vero se la voce |itemIndex| si puo' scegliere. Una voce spenta si vede spenta e
// non fa niente: il cursore non ci si ferma, il clic non la sceglie, `Invio` non la
// esegue. E' una regola sola per le tre vie d'ingresso, ed e' il motivo per cui sta
// qui invece che in ciascuna di esse.
bool       GroupMenuItemEnabled(const GroupMenu& menu, int itemIndex);
bool       GroupMenuSelectedEnabled(const GroupMenu& menu);
MenuAction GroupMenuKeyDown(GroupMenu& menu, WPARAM key);
void       GroupMenuChar(GroupMenu& menu, wchar_t character);

// Il menu piu' profondo aperto, o niente. Il piu' profondo per primo: un controllo
// `Choice` aperto sta sopra le opzioni, e finche' c'e' e' lui a prendere tastiera e
// clic.
GroupMenu* OpenMenu(AppState& app);

// --- geometria ---------------------------------------------------------------
//
// Le stesse risposte servono a disegnare, a scorrere e a sapere dove si e' cliccato:
// l'altezza di una riga, la prima visibile, e il pannello di ogni menu.
float MenuRowHeight(const Layout& layout, const GroupMenu& menu);
int   GroupMenuFirstVisible(const GroupMenu& menu, float listTop, float listBottom, float rowHeight);
Rect  GroupMenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu);
Rect  ColumnMenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu);
Rect  MenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu);

// Il controllo di una riga di opzione (un interruttore, uno slider, una scelta): la
// larghezza non dipende dalla riga, ed e' la stessa per il disegno, per il clic e per
// il trascinamento di uno slider.
float OptionControlWidth(const GroupMenuItem& item, float scale);
Rect  OptionControlRect(const Layout& layout, const GroupMenuItem& item, const Rect& rowRect);

// --- disegno -----------------------------------------------------------------
//
// Il clic non e' qui: `GroupMenuClick` chiede dove sono le righe a `HitTest`, che e'
// il registro dei bersagli e sta con l'input. Il componente sa **dove** sono le sue
// righe; cosa vuol dire premerle lo dice `main.cpp`.
void GroupMenuDraw(AppState& app, const Layout& layout, GroupMenu& menu);
} // namespace Bolt
