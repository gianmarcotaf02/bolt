// ---------------------------------------------------------------------------
// L'ispettore: i metadati di una voce, la sua anteprima, e cosa c'e' dentro una
// cartella.
//
// Era in `main.cpp`, dentro il namespace anonimo, ed e' il pezzo di vista piu'
// grosso uscito da li' dopo la lista. Il pannello di destra e' tutto qui: le righe
// dei metadati, l'immagine grande decodificata, il visualizzatore di testo, e il
// contenuto della cartella scelta.
//
// Due funzioni escono da qui, e sono due perche' due sono i chiamanti:
//
//   * `DrawInspector` — il pannello, chiamato dal fotogramma;
//   * `BuildInspectorRows` — le righe, chiamate anche dal pannello delle proprieta',
//     che e' la **stessa** informazione in un'altra finestra. Due calcoli separati
//     mostrerebbero prima o poi due numeri diversi.
//
// La direzione delle dipendenze e' quella degli altri moduli di vista: questo file
// include `app_state.h`, `app_ui.h` e `view_list.h` — per il cursore, il percorso e
// l'icona di una voce, che sono domande della lista — e non conosce ne' i menu ne' i
// comandi.
// ---------------------------------------------------------------------------
#pragma once

#include <vector>

#include "app_state.h"

namespace Bolt
{
// Le righe che descrivono |entry|, nell'ordine in cui si leggono. Non disegnano e non
// sanno dove finiranno: quello che sanno e' chi le mostra.
void BuildInspectorRows(AppState& app, const Entry& entry, std::vector<InspectorRow>* out);

// Il pannello dell'ispettore: metadati, anteprima, contenuto della cartella scelta.
void DrawInspector(AppState& app, const Layout& layout);
} // namespace Bolt
