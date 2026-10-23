// ---------------------------------------------------------------------------
// Le cartelle che non sono percorsi.
//
// Il cestino, "Questo PC", il pannello di controllo: la shell li chiama *spazio
// dei nomi*, e non hanno un percorso di file. Si raggiungono con un nome che
// comincia per `::` — `::{645FF040-5081-101B-9F08-00AA002F954E}` e' il cestino —
// e si aprono con `SHParseDisplayName`, che quel nome lo sa leggere.
//
// Modo per ricostruirlo senza scriverne i CLSID a mano:
//
//     SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, ...)  -> PIDL
//     SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING) -> "::{...}"
//
// Perche' serve: **il cestino e' l'unica cartella in cui "ripristina" esiste**, e
// il ripristino e' una voce del menu contestuale della shell sul singolo oggetto.
// Con la navigazione per percorso quella cartella era irraggiungibile, e con essa
// il ripristino — che era fermo da M7 proprio per questo.
//
// Qui si legge l'elenco. La conversione in `Entry` e tutto il resto — lista,
// ordinamento, filtro, gruppi — stanno in `main.cpp`, e da li' in poi queste voci
// sono voci come le altre: e' l'unico modo di non avere due liste diverse.
//
// Quello che NON si fa, e va detto: sui nomi di spazio dei nomi le operazioni per
// percorso (copia, taglia, elimina, rinomina) non valgono. Li' si usano i verbi
// della shell, che quegli oggetti li conoscono.
//
// Una **cartella** dentro il cestino si apre. Per farlo la sua posizione e' il PIDL
// codificato in una stringa (`EncodePidl`): dentro il cestino il nome assoluto di
// una cartella riciclata punta al file riciclato, che e' un'altra voce, quindi per
// nome non si torna su quella cartella. Il PIDL si'.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <shobjidl.h>

#include <string>
#include <vector>

namespace ShellLocation
{
// Vero se |location| non e' un percorso di file. Il prefisso `::` e' come la shell
// scrive i nomi dello spazio dei nomi.
bool IsNamespace(const std::wstring& location);

// Un PIDL in una stringa, per le posizioni che **una stringa non le sa nominare**.
// Dentro il cestino una cartella ha un nome assoluto (`SIGDN_DESKTOPABSOLUTEPARSING`)
// che punta al file riciclato — un'altra cosa — quindi non si puo' navigare per
// nome. Il PIDL invece la identifica, e `shell:pidl:<esadecimale>` e' il modo di
// portarlo dentro una `std::wstring` senza cambiare il tipo di `app.directory`.
//
// Esiste solo per questo: le posizioni che hanno un nome continuano ad averlo.
std::wstring EncodePidl(const std::vector<BYTE>& pidl);

// Il PIDL di una posizione: decodificato se e' un `shell:pidl:`, altrimenti chiesto
// a `SHParseDisplayName`. Va liberato con `CoTaskMemFree`. Nullo se la shell non la
// conosce. **Tutti** i punti che leggono una posizione passano di qui: due modi di
// interpretare la stessa stringa sono due comportamenti diversi.
PIDLIST_ABSOLUTE Parse(const std::wstring& location);

// Il nome da mostrare — "Cestino" dove un percorso mostrerebbe se stesso. Vuoto
// se la shell non riconosce la posizione.
std::wstring DisplayName(const std::wstring& location);

// Il nome di spazio dei nomi di una cartella nota, cosi' "Cestino" non ha un CLSID
// scritto dentro il nostro codice.
std::wstring ParsingNameOf(REFKNOWNFOLDERID id);

// L'"Accesso rapido" di Esplora risorse: la cartella di spazio dei nomi in cui la
// shell tiene quello che l'utente ha fissato.
std::wstring QuickAccessName();

// La posizione che contiene |location|. Vuoto se non c'e' — o se la shell non la
// sa nominare: senza un nome non si potrebbe navigare.
std::wstring ParentOf(const std::wstring& location);

// Quello che serve a riempire una riga della lista. Il resto — il tipo, la
// dimensione in lettere, le date — lo scrive `main.cpp` con le funzioni che usa
// gia' per le cartelle normali: due formati diversi nella stessa colonna si
// vedrebbero subito.
struct Item
{
    std::wstring name;      // come si vede nella lista
    std::wstring location;  // nome assoluto, per navigarci
    std::string  extension; // minuscola, senza punto
    uint64_t     bytes = 0;
    FILETIME     modified{};
    FILETIME     created{};
    FILETIME     accessed{};
    bool         isDirectory = false;

    // L'identita' vera della voce. Per una voce di spazio dei nomi una stringa non
    // basta: il "nome assoluto" di un file nel cestino e' il percorso del file
    // dentro `$Recycle.Bin`, che e' **un'altra voce** — il file vero, non quello
    // riciclato — e la shell, se le si chiede il menu di quel nome, risponde col
    // menu di quel file. Senza il PIDL, "Ripristina" non esiste.
    std::vector<BYTE> pidl;

    // Vero se |location| porta alla stessa voce, e non a un'altra: e' quello che
    // rende una cartella apribile. Per un'unita' si'; per una cartella dentro il
    // cestino no, per la ragione di sopra.
    bool navigable = false;
};

// Un volume: se un percorso **e'** la radice di un volume, il file system e lo
// spazio. Non sono proprieta' del file ma del volume che lo contiene, e si chiedono
// al sistema — lo dice anche `docs/03`: l'occupazione dei volumi si ottiene con
// `GetDiskFreeSpaceExW`.
//
// Sta qui e non nell'ispettore perche' i due posti che lo chiedono devono ricevere
// la **stessa** risposta: l'elenco, che decide se una voce e' un'unita' (e quindi se
// la sua icona dipende dal disco), e l'ispettore, che mostra file system e spazio.
struct Volume
{
    bool        isVolume = false;
    std::string fileSystem;
    std::string freeText;
    std::string totalText;
    float       usedFraction = 0.0f;
};

Volume VolumeOf(const std::wstring& location);

// Le voci della cartella. Vuoto con |error| pieno se non si apre: una cartella che
// non si apre e' un'informazione, non un elenco vuoto.
std::vector<Item> Read(const std::wstring& location, std::string* error);

// Una colonna come la mostra la shell: nome e valore.
struct Column
{
    std::wstring title;
    std::wstring value;
};

// **Tutte** le colonne che la shell ha per una voce, con i nomi che usa lei. Per un
// file riciclato sono "Posizione originale", "Data eliminazione" e il tipo — cose
// che non sono proprieta' di sistema (`propkey.h` non le ha) e che quindi non si
// possono chiedere come le altre: si chiedono alla cartella del cestino, che le ha.
//
// Si leggono tutte invece di sceglierne due per numero: l'ordine delle colonne e'
// fisso ma i nomi sono tradotti, e cercare "la seconda" sarebbe indovinare — su una
// macchina in un'altra lingua l'indovinello cambierebbe risposta.
std::vector<Column> Columns(const std::vector<BYTE>& pidl);

// Quando una voce riciclata e' stata buttata e da dove veniva. Le chiede la colonna —
// `Date Deleted` e `Original Location`, che `docs/03` elenca e che il file system non
// sa — e non l'ispettore, che per quelle voci mostra gia' tutte le colonne della shell.
//
// Il tempo torna in **UTC**, come lo da' la shell: chi lo scrive usa `FormatFileTime`,
// che la conversione in locale la fa li' e una volta sola.
//
// Falso se la voce non e' nel cestino, o se questa versione di Windows non conosce le
// due proprieta': in tutti e due i casi i campi restano vuoti, e la colonna scrive `--`.
bool RecycleFacts(const std::vector<BYTE>& pidl, FILETIME* deleted,
                  std::string* originalLocation);
} // namespace ShellLocation
