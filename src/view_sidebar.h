// ---------------------------------------------------------------------------
// La barra laterale.
//
// Era in `main.cpp`, dentro il namespace anonimo, dove nessun altro file poteva
// usarla: e' il primo pezzo di **vista** uscito da li'. Le voci le costruisce
// `BuildSidebarRows` e le disegna `DrawSidebar`, e la separazione e' quella che
// conta: cos'e' una riga (un percorso, un'icona, quanto e' pieno un disco) e' un
// conto, disegnarla e' un'altra cosa, e chi vuole sapere dove si clicca non deve
// leggere il codice del disegno.
//
// Non sa cosa sia un tab, una lista o un inspector: riceve lo stato e la geometria.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// Una riga della barra laterale: un'intestazione di sezione, oppure una voce.
struct SidebarRow
{
    bool         header = false;
    std::string  title;     // per un'intestazione: il nome che si legge
    std::string  key;       // per un'intestazione: l'identita', che non si traduce
    std::string  label;
    std::wstring path;
    std::wstring icon;      // l'oggetto della shell da cui farsi disegnare
    bool         volume = false;
    float        usedFraction = 0.0f;
    std::string  freeText;
    // Fissata da **noi** (`Fissa in...`) e non dalla shell: e' l'unica delle due che
    // si puo' togliere, e si disegna con il segno che lo dice.
    bool         pinned = false;
};

// Riempie l'elenco: le sezioni, e dentro ognuna le voci.
void BuildSidebarRows(const AppState& app, std::vector<SidebarRow>* rows);

// Disegna la barra laterale, e registra i bersagli dei clic mentre la disegna.
void DrawSidebar(AppState& app, const Layout& layout);
} // namespace Bolt
