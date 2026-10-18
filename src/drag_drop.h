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

// Lo stesso, ma da **PIDL** invece che da percorsi. Serve per le cartelle che non
// sono percorsi: il nome di una voce del cestino e' il percorso del file dentro
// `$Recycle.Bin`, quindi trascinare quel nome significa trascinare un altro file.
// Da un PIDL invece la shell sa risalire all'oggetto vero. Il chiamante lo rilascia.
IDataObject* DataObjectForPidls(HWND owner, const std::vector<const BYTE*>& pidls);

// Trascina |paths| fuori dalla finestra. |effect| riceve quello che la
// destinazione ha dichiarato: `DROPEFFECT_NONE` se non e' successo niente.
// `DROPEFFECT_MOVE` dice al chiamante che gli originali non ci sono piu' e la
// vista e' da rifare.
void DragOut(HWND window, const std::vector<std::wstring>& paths, DWORD* effect);

// Trascina un oggetto dati gia' costruito: |count| e' quante voci porta, e serve
// solo al registro diagnostico.
void DragOutData(HWND window, IDataObject* data, size_t count, DWORD* effect);

// --- verso l'interno --------------------------------------------------------

// Registra la finestra come destinazione di caduta, e la toglie. Vanno chiamate
// quando OLE e' inizializzato e la finestra esiste, nell'ordine: `Register`
// restituisce falso se il sistema non vuole saperne, e chi chiama lo dice
// all'utente invece di lasciare un trascinamento che non arriva.
bool Register(HWND window);
void Unregister(HWND window);

// --- quello che il modulo non sa, e chiede a chi conosce la vista -----------

// Cosa si puo' fare di quello che si lascia cadere su un punto. La terza non e' una
// variante della seconda, e chiamarla "destinazione" sarebbe stato il modo di
// sbagliarla: il cestino non e' una cartella *dove i file finiscono*, e' il gesto
// di disfarsene — non ha senso copiarci dentro, e Ctrl non deve trasformarlo in una
// copia.
enum class DropVerb
{
    None,       // li' non si lascia cadere niente
    DropInto,   // metti dentro la cartella |path|
    Discard,    // buttali via
};

struct Target
{
    DropVerb     verb = DropVerb::None;
    std::wstring path;             // per DropInto: la cartella
    bool         ontoRow = false;  // evidenziazione piena (una riga) o leggera (il fondo)
    int          sidebarRow = -1;  // voce del riquadro di navigazione, -1 se non viene da li'

    bool Empty() const { return verb == DropVerb::None; }
};

// La destinazione sotto il punto |client| (coordinate cliente).
Target TargetAt(HWND window, POINT client);

// Evidenzia la destinazione, o toglie l'evidenziazione se non ce n'e' una.
void Highlight(HWND window, const Target& target);

// Esegue la caduta di |sources| su |target|. |move| vale per DropInto.
//
// Restituisce vero se l'operazione e' stata **accettata**. Non e' una comodita': chi
// ha mollato il tasto legge dalla destinazione se la cosa e' andata a buon fine, e
// con `DROPEFFECT_MOVE` cancella gli originali. Una caduta rifiutata e dichiarata
// riuscita sarebbe una perdita di dati, quindi il rifiuto va detto.
bool Drop(HWND window, const std::vector<std::wstring>& sources, const Target& target, bool move);
} // namespace DragDrop
