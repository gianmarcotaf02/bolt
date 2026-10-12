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
} // namespace ShellLocation
