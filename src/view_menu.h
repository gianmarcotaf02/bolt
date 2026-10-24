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

// Dove comincia l'elenco delle righe dentro un pannello. La testata del comando,
// quando c'e', sta fra il campo di ricerca e la prima riga, e sposta l'elenco in
// basso: disegno e clic devono leggere **questa** funzione, o il puntatore finirebbe
// su una riga diversa da quella che si vede.
float MenuListTop(const Layout& layout, const Rect& panel, const GroupMenu& menu);
// Dove finisce l'elenco dentro il pannello. La distanza dal bordo sta qui e non
// in ogni chiamante: disegno, clic, rotella e barra devono contare le stesse
// righe visibili.
float MenuListBottom(const Layout& layout, const Rect& panel);
// La prima riga visibile dell'elenco **e** di quanto e' gia' salita: con lo
// scorrimento in pixel una riga sta a meta' fra due bordi, e il disegno e il
// clic devono leggere la stessa coppia. Due calcoli della stessa riga sono due
// occasioni di non essere d'accordo su cosa sta sotto il puntatore.
struct MenuScrollView
{
    int   first  = 0;
    float offset = 0.0f;   // quanto la riga `first` sporge sopra il bordo
};
MenuScrollView GroupMenuScrollViewOf(const GroupMenu& menu, float listTop, float listBottom,
                                     float rowHeight);

// --- lo scorrimento in pixel (pannello delle opzioni) ------------------------
// Quanto e' lungo l'elenco e quanto se ne vede: dalle due misure viene il fondo
// dello scorrimento, che chi lo muove deve conoscere prima di muoverlo.
float GroupMenuMaxScroll(const GroupMenu& menu, float rowHeight, float room);

// Un passo di rotella: |delta| pixel, positivo verso l'alto. Il bersaglio si
// ferma ai due capi, e il contenuto lo raggiunge animato invece di saltarci.
void GroupMenuScrollBy(GroupMenu& menu, float delta, float rowHeight, float room);

// Un fotogramma di scorrimento: avvicina l'elenco al bersaglio e riporta in
// vista il cursore, se e' stato mosso da chi legge. Vero se sta ancora
// scivolando, cioe' se serve un altro fotogramma.
bool GroupMenuAdvanceScroll(GroupMenu& menu, float rowHeight, float room, bool animated);
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
