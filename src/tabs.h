// ---------------------------------------------------------------------------
// L'albero dei tab: chi e' chi, e in che ordine si legge.
//
// Un tab ha un'**identita'** e un `parentId`, non una posizione: gli indici
// scivolano appena se ne chiude uno, e un albero tenuto per indici si sbaglia da
// solo — il figlio del tab 3 diventa il figlio di un altro. Qui sta la parte
// **pura** di quel modello: cercare un tab, trovarne la radice, dire se uno sta
// sotto un altro, appiattire l'albero nell'ordine in cui si vede.
//
// Non ci sta chi apre, chiude e attiva un tab: quelle toccano la navigazione, che
// vive ancora in `main.cpp`, e si sposteranno insieme a lei.
// ---------------------------------------------------------------------------
#pragma once

#include <utility>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// L'indice nell'elenco dei tab, o -1 se quell'identita' non c'e' piu'.
int TabById(const AppState& app, int id);

// L'identita' del tab in cima all'albero a cui |id| appartiene: se e' gia' una
// radice, se stesso.
int TabRootId(const AppState& app, int id);

// Vero se |id| sta sotto |ancestorId|. Serve a due cose che devono dire la stessa
// cosa: non disegnare un tab dentro un proprio discendente, e non chiudere un ramo
// che non e' il suo.
bool TabIsDescendantOf(const AppState& app, int id, int ancestorId);

// L'albero appiattito nell'ordine in cui si vede: indice del tab e rientro.
std::vector<std::pair<int, int>> TabOrder(const AppState& app);
} // namespace Bolt
