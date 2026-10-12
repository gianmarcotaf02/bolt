// ---------------------------------------------------------------------------
// Trascinamento: file che arrivano da fuori, e file che escono da qui.
//
// Tutto passa da OLE, che e' il motivo per cui si puo' trascinare da Esplora
// risorse a Bolt e viceversa: il trasferimento non e' un accordo fra due
// programmi, e' un oggetto dati che il sistema consegna alla finestra sotto il
// puntatore. Chi riceve dichiara cosa e' disposto a fare, chi manda decide se
// mollare.
//
//   verso l'interno  IDropTarget   registrato sulla finestra: DragEnter,
//                                  DragOver, DragLeave, Drop
//   verso l'esterno  IDropSource   + DoDragDrop, che gira finche' il tasto resta
//                                  premuto
//
// **Cosa c'e' sotto il puntatore non lo sa questo modulo**: la destinazione di una
// caduta dipende dalla vista (una riga di cartella, il fondo della lista), e la
// vista sta in `main.cpp`. Il modulo conosce OLE, i percorsi e i PIDL, e chiede
// il resto a tre funzioni dichiarate in fondo a `drag_drop.h`.
//
// L'oggetto dati e' quello della shell (`SHCreateDataObject`), non uno nostro: e'
// lo stesso che scrive Esplora risorse — CF_HDROP per i programmi vecchi, l'elenco
// dei PIDL per la shell — quindi una cartella o un altro file manager lo accettano
// senza sapere chi gliel'ha mandato.
//
// La **regola copia/sposta** e' quella di Esplora risorse, ed e' fatta di tre casi:
// Ctrl copia, Shift sposta, e senza tasti si guarda il volume — stesso volume
// sposta, volume diverso copia. Un file trascinato nella cartella dove e' gia' non
// si sposta: li' non c'e' niente da fare, e dirlo e' meglio che far finta.
//
// Nessun oggetto qui dentro e' thread-safe oltre a se stesso, e non serve: OLE
// consegna le chiamate al thread che ha registrato la finestra.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <objidl.h>

#include <string>
#include <vector>

namespace DragDrop
{
// --- verso l'esterno --------------------------------------------------------

// |paths| come oggetto dati della shell. Il chiamante lo rilascia.
IDataObject* DataObjectFor(const std::vector<std::wstring>& paths);

// Trascina |paths| fuori dalla finestra. |effect| riceve quello che la
// destinazione ha dichiarato: `DROPEFFECT_NONE` se non e' successo niente.
// `DROPEFFECT_MOVE` dice al chiamante che gli originali non ci sono piu' e la
// vista e' da rifare.
void DragOut(HWND window, const std::vector<std::wstring>& paths, DWORD* effect);

// --- verso l'interno --------------------------------------------------------

// Registra la finestra come destinazione di caduta, e la toglie. Vanno chiamate
// quando OLE e' inizializzato e la finestra esiste, nell'ordine: `Register`
// restituisce falso se il sistema non vuole saperne, e chi chiama lo dice
// all'utente invece di lasciare un trascinamento che non arriva.
bool Register(HWND window);
void Unregister(HWND window);

// --- quello che il modulo non sa, e chiede a chi conosce la vista -----------

// La cartella sotto il punto |client| (coordinate cliente). Vuota se li' non si
// puo' lasciare cadere niente. |ontoRow| dice se la destinazione e' una riga
// dell'elenco o il fondo della lista.
std::wstring TargetAt(HWND window, POINT client, bool* ontoRow);

// Evidenzia la destinazione, o toglie l'evidenziazione se |target| e' vuoto.
void Highlight(HWND window, const std::wstring& target, bool ontoRow);

// Esegue la caduta di |sources| dentro |target|, copiando o spostando.
void Drop(HWND window, const std::vector<std::wstring>& sources, const std::wstring& target,
          bool move);
} // namespace DragDrop
