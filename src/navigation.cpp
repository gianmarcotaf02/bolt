// ---------------------------------------------------------------------------
// La navigazione: leggere una cartella, e l'albero dei tab.
//
// Due cose che sembrano due e sono una: **dove si sta** e **quali posti si sono
// aperti**. `LoadDirectory` riempie la lista e aggiorna la scheda; i tab tengono una
// pila di cartelle per ognuna, col genitore e il posto nell'ordine di lettura
// (`tabs.*`, la parte pura). Chi apre un posto nuovo spesso deve fare tutte e due, ed
// e' per questo che stanno nello stesso file: separarle vorrebbe dire una chiamata
// all'altra per ogni navigazione.
//
// Uscito da `main.cpp` il 2026-10-18, ed e' uscito **quasi pulito**: l'unica cosa che
// chiedeva a `main.cpp` era `HitTest`, che ora e' `input_targets.*` — spostato apposta,
// perche' la navigazione deve sapere dove sono le linguette per il trascinamento.
// ---------------------------------------------------------------------------
#include "navigation.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app_ui.h"
#include "diag.h"
#include "home.h"
#include "input_targets.h"
#include "language.h"
#include "shell_location.h"
#include "tabs.h"
#include "view_list.h"
#include "view_statusbar.h"

namespace Bolt
{
// --- directory reading -----------------------------------------------------
//
// Le voci di una cartella che non e' un percorso. Il lavoro grosso lo fa
// `ShellLocation`; qui si traduce nel tipo che usa la lista, con le stesse funzioni
// di formattazione delle cartelle normali: da qui in poi una voce del cestino e'
// una voce come le altre, e ordinamento, filtro e gruppi non sanno da dove viene.
std::vector<Entry> ReadNamespaceDirectory(const std::wstring& directory, std::string* error)
{
    std::vector<Entry> entries;
    for (const ShellLocation::Item& item : ShellLocation::Read(directory, error))
    {
        Entry entry;
        entry.wideName    = item.name;
        entry.name        = ToUtf8(item.name);
        entry.isDirectory = item.isDirectory;
        entry.bytes       = item.bytes;
        entry.modified    = item.modified;
        entry.created     = item.created;
        entry.accessed    = item.accessed;
        entry.extension   = item.extension;
        entry.location    = item.location;
        entry.pidl        = item.pidl;
        entry.navigable   = item.navigable;

        // Le due cose che solo il cestino sa dire: quando e' stata buttata e da dove
        // veniva. Si chiedono per **ogni** voce di spazio dei nomi e non solo li': una
        // cartella che non e' un percorso non ha un altro modo di rispondere, e fuori dal
        // cestino la shell dice subito che non lo sa (`ShellLocation::RecycleFacts`), che
        // costa una chiamata e restituisce niente.
        if (!entry.pidl.empty())
        {
            FILETIME deleted{};
            std::string from;
            if (ShellLocation::RecycleFacts(entry.pidl, &deleted, &from))
            {
                const bool dated = deleted.dwLowDateTime != 0 || deleted.dwHighDateTime != 0;
                // Il tempo arriva in UTC: `FormatFileTime` lo porta in locale, ed e'
                // l'unica funzione che scrive una data in questo programma.
                entry.deleted     = deleted;
                entry.deletedText = dated ? FormatFileTime(deleted, /*withTime=*/false) : "";
                entry.originalText = from;
            }

        }

        // Un'unita' dentro "Questo PC" e' un disco, non una cartella, e l'icona
        // gliela sa dare solo la shell. Si decide **adesso**, una volta per cartella
        // aperta: chiedere il tipo di un'unita' puo' far girare un lettore ottico, e
        // una riga si ridisegna sessanta volte al secondo.
        entry.volumeRoot =
            !entry.location.empty() && ShellLocation::VolumeOf(entry.location).isVolume;

        // Un tempo a zero non e' il 1601: e' "la shell non lo sa". Scriverlo come
        // data faceva comparire `1601-01-01` in ogni riga di "Questo PC", che e'
        // una data vera solo per chi guarda il calendario di Windows.
        const bool dated = item.modified.dwLowDateTime != 0 || item.modified.dwHighDateTime != 0;
        entry.typeText     = ShellTypeName(entry.extension, entry.isDirectory);
        entry.sizeText     = entry.isDirectory ? "" : HumanBytes(entry.bytes);
        entry.modifiedText = dated ? FormatFileTime(entry.modified, /*withTime=*/false) : "";
        entry.modifiedFull = dated ? FormatFileTime(entry.modified, /*withTime=*/true) : "";
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<Entry> ReadDirectory(const std::wstring& directory, std::string* error,
                                 bool longPaths)
{
    std::vector<Entry> entries;
    error->clear();

    // Il cestino non si elenca con FindFirstFile, e non e' un percorso: le sue
    // voci le sa solo la shell.
    if (ShellLocation::IsNamespace(directory))
        return ReadNamespaceDirectory(directory, error);

    WIN32_FIND_DATAW data{};
    // `LongPathsSupport`: il prefisso lo vuole Win32, non la shell, e si mette qui
    // — davanti a `FindFirstFile`, che e' l'unica cosa che parla in questo punto.
    const std::wstring pattern = IOPath(directory, longPaths) + L"\\*";
    HANDLE search = FindFirstFileExW(pattern.c_str(),
                                     FindExInfoBasic,
                                     &data,
                                     FindExSearchNameMatch,
                                     nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE)
    {
        // Retry without the large-fetch hint: some network providers reject it.
        search = FindFirstFileW(pattern.c_str(), &data);
    }

    if (search == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        *error = (code == ERROR_ACCESS_DENIED)
                     ? Tr("Accesso negato.", "Access denied.")
                     : Tr("Non si riesce a leggere questa cartella.",
                          "Cannot read this folder.");
        return entries;
    }

    do
    {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;

        Entry entry;
        entry.wideName    = name;
        entry.name        = ToUtf8(name);
        entry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.bytes       = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        entry.modified    = data.ftLastWriteTime;
        entry.created     = data.ftCreationTime;
        entry.accessed    = data.ftLastAccessTime;

        const size_t dot = entry.name.rfind('.');
        if (!entry.isDirectory && dot != std::string::npos && dot + 1 < entry.name.size())
        {
            entry.extension = entry.name.substr(dot + 1);
            std::transform(entry.extension.begin(), entry.extension.end(), entry.extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }

        entry.typeText     = ShellTypeName(entry.extension, entry.isDirectory);
        entry.sizeText     = entry.isDirectory ? "" : HumanBytes(entry.bytes);
        entry.modifiedText = FormatFileTime(entry.modified, /*withTime=*/false);
        entry.modifiedFull = FormatFileTime(entry.modified, /*withTime=*/true);
        entries.push_back(std::move(entry));
    }
    while (FindNextFileW(search, &data) != FALSE);

    FindClose(search);
    return entries;
}

// `Toggle Subfolders`: anche il contenuto delle sottocartelle nella stessa lista, con
// il genitore accanto al nome. Due limiti, dichiarati: la **profondita'** e il numero
// di voci. Senza, accendere questa modalita' su `C:\Windows` leggerebbe decine di
// migliaia di cartelle durante il primo fotogramma, e la finestra non risponderebbe
// piu'. Quando un limite morde lo si dice: un elenco a meta' che sembra completo e'
// peggio di un elenco corto.
constexpr int    kSubfolderDepth = 3;
constexpr size_t kSubfolderCap   = 2000;

// Quante voci sono arrivate dalle sottocartelle, e se il limite ha morso.
struct SubfolderResult
{
    size_t added = 0;
    bool   capped = false;
};

SubfolderResult ExpandSubfolders(AppState& app)
{
    SubfolderResult result;
    if (!app.showSubfolders || IsHome(app.directory) || ShellLocation::IsNamespace(app.directory))
        return result;

    // Una coda di cartelle da leggere, ognuna con la **catena** che la porta alla
    // cartella aperta: e' quella catena che diventa il genitore accanto al nome.
    struct Pending
    {
        std::wstring path;
        std::string  chain;
        int          depth = 0;
    };
    std::vector<Pending> queue;

    for (const Entry& entry : app.entries)
        if (entry.isDirectory && entry.location.empty())
            queue.push_back({JoinPath(app.directory, entry.wideName), entry.name, 1});

    std::vector<Entry> added;
    for (size_t head = 0; head < queue.size(); ++head)
    {
        if (added.size() >= kSubfolderCap)
        {
            result.capped = true;
            break;
        }

        const Pending item = queue[head];
        if (item.depth > kSubfolderDepth)
        {
            result.capped = true;
            continue;
        }

        std::string error;
        std::vector<Entry> children = ReadDirectory(item.path, &error, app.longPaths);
        // Una sottocartella che non si legge non ferma le altre: "accesso negato" su
        // una non vuol dire niente sulle sue sorelle.
        if (!error.empty())
            continue;

        for (Entry& child : children)
        {
            if (added.size() >= kSubfolderCap)
            {
                result.capped = true;
                break;
            }

            child.fullPath   = JoinPath(item.path, child.wideName);
            child.parentHint = item.chain;

            // Le figlie si leggono al giro dopo, con la catena che le nomina.
            if (child.isDirectory && item.depth < kSubfolderDepth)
                queue.push_back({child.fullPath, item.chain + "\\" + child.name, item.depth + 1});

            added.push_back(std::move(child));
        }
    }

    result.added = added.size();
    for (Entry& entry : added)
        app.entries.push_back(std::move(entry));
    return result;
}

void UpdateWindowTitle(AppState& app)
{
    if (app.window == nullptr)
        return;
    const std::wstring title = L"Bolt — " + LocationLabel(app, app.directory);
    SetWindowTextW(app.window, title.c_str());
}

// |recordHistory| is false only when the move is itself a history step, or
// walking back would push the place you are leaving onto the stack again.
void LoadDirectory(AppState& app, const std::wstring& directory, bool recordHistory)
{
    const std::wstring normalised = directory.empty() ? L"" : directory;
    // Il filtro non segue l'utente in un'altra cartella — un filtro rimasto da un
    // altro elenco sembrerebbe una cartella vuota — ma **ricaricare la stessa
    // cartella non e' cambiare cartella**: premere F5 non deve svuotare la casella
    // del filtro, che sta sotto gli occhi ed e' una cosa che l'utente ha scritto.
    const bool sameFolder = (normalised == app.directory);
    const bool home = IsHome(normalised);
    // Da quale delle due sponde si arriva: la Home ha un modo di visualizzazione suo,
    // e le cartelle hanno il loro (`docs/04` M12).
    const bool comingFromHome = IsHome(app.directory);

    // La Home non si legge dal disco e non e' una cartella: le sue righe sono le
    // destinazioni che conosciamo (fissaggi, recenti, cartelle note). Rifarle qui
    // significa che F5 la ricostruisce come una cartella qualsiasi.
    if (home)
    {
        app.entries   = BuildHomeEntries(app);
        app.errorText.clear();
    }
    else
    {
        app.entries = ReadDirectory(normalised, &app.errorText, app.longPaths);
        // `Toggle Subfolders`: le voci della cartella ci sono, e se la modalita' e'
        // accesa si aggiunge quello che sta dentro le sue sottocartelle. Si fa qui e
        // non nel comando perche' vale a ogni lettura — `F5`, il sorvegliante della
        // cartella, un tab riaperto — e un modo che si accende una volta e poi si
        // dimentica al primo ricaricamento sarebbe un mezzo modo.
        const SubfolderResult subfolders = ExpandSubfolders(app);
        if (subfolders.capped)
            ReportStatus(app, TrW(L"Sottocartelle: elenco fermato a un limite",
                                  L"Subfolders: list stopped at a limit"));
    }
    app.directory = normalised;
    app.previews.CancelPending();
    app.stats.CancelQueued();
    // Le tre colonne del volume leggono da qui: si chiede una volta per cartella, non
    // a ogni fotogramma.
    RefreshVolumeFacts(app);

    if (!sameFolder)
    {
        // Se questa cartella ha un layout salvato, e' quello che si rivede: vista,
        // misura, ordinamento, gruppi e larghezze delle colonne. Una cartella senza
        // layout lascia quello che si sta usando, invece di riportare tutto a
        // "dettagli" — il salvataggio e' esplicito, quindi chi non l'ha chiesto non
        // se lo trova.
        const bool layoutApplied = ApplyFolderLayout(app);

        // La **Home nasce a griglia** (icona e nome, non una tabella di colonne), ma
        // solo se quella cartella non ha un layout suo: chi ha salvato una vista per la
        // Home ha scelto, e la scelta vince sul valore di partenza. Uscendo dalla Home si
        // ritrova il modo delle cartelle, che nel frattempo e' rimasto suo.
        if (home && !comingFromHome && !layoutApplied)
            app.viewMode = app.homeViewMode;
        else if (!home && comingFromHome && !layoutApplied)
            app.viewMode = app.viewModeOutsideHome;
        app.viewDirty = true;

        app.filter.clear();
        app.filterActive = false;
        // I valori di una colonna sono quelli della cartella che si sta lasciando:
        // il pannello si chiude invece di filtrare su un elenco che non c'e' piu'.
        app.columnMenu.open = false;
        app.columnPanel     = -1;
    }

    app.scroll = 0.0f;
    app.selection.clear();
    app.cursor.clear();
    app.selectionAnchor.clear();
    app.rectSelecting = false;
    // La riga sotto il puntatore e' un **indice**, e questo e' un elenco nuovo: la
    // riga 3 era un file e adesso ne e' un altro. Cursore e selezione si ritrovano
    // per nome perche' un nome ce l'hanno; il sovrappasso no — il puntatore sta su un
    // **pixel**, non su un file — quindi si azzera e torna al primo movimento del
    // mouse. Senza questo azzeramento `entries[hovered]` legge fuori dal vettore: una
    // cartella con una voce sola, aperta dopo un'altra, faceva cadere l'applicazione.
    app.hovered    = -1;
    app.hoverStart = NowSeconds();

    SortEntries(app);

    // Le colonne si adattano al contenuto della cartella che si e' appena letta, finche'
    // non si e' scelta una larghezza a mano: "adatta" e' il valore di partenza.
    if (app.autoFitColumns)
        FitColumns(app);

    // Entrando in una cartella **non c'e' niente di scelto**: prima la prima riga si
    // selezionava da sola, e un `Canc` appena arrivati cancellava un file che nessuno
    // aveva scelto. Il cursore si posa al primo tasto di movimento (`MoveSelection`
    // parte dalla prima riga quando non ce l'ha), e il primo clic sceglie.
    app.viewDirty = true;

    if (recordHistory)
    {
        // Walking somewhere new discards the forward stack, the way every
        // browser and file manager does.
        if (app.historyIndex + 1 < static_cast<int>(app.history.size()))
            app.history.resize(static_cast<size_t>(app.historyIndex + 1));
        app.history.push_back(normalised);
        app.historyIndex = static_cast<int>(app.history.size()) - 1;
    }

    // Il ranking: una visita pesa 3 (lo stesso peso che da' il riferimento alla
    // navigazione), il punteggio satura a 10 e i fratelli decadono. La formula e'
    // in `src/recents.h`; qui si dice soltanto che questa e' una visita.
    //
    // StoreRecents ha tre modi (LocalDisk, Persistent, Session) e nessuno dei tre
    // scrive su disco: `docs/06`. La scelta e' Session — in memoria — e si cambia
    // dal pannello delle opzioni, dove si legge che non si salva.
    //
    // La Home non e' una cartella da riproporre fra i recenti: registrarla
    // vorrebbe dire una voce che riporta alla pagina di avvio, cioe' un giro su se
    // stessi in cima all'elenco delle destinazioni.
    if (!home)
        app.recents.Touch(normalised);

    UpdateWindowTitle(app);

    // La sorveglianza segue la navigazione: c'e' un watcher solo, e guarda la
    // cartella aperta. Se e' la stessa non fa niente, quindi si puo' chiamare qui
    // senza pensarci — e va chiamata **dopo** che `app.directory` e' la cartella
    // nuova, o sorveglierebbe quella di prima.
    //
    // Sulla Home il bersaglio si **svuota**: non c'e' niente da sorvegliare, e
    // lasciare l'ultima cartella vorrebbe dire continuare a raccogliere notifiche
    // su qualcosa che non si sta guardando.
    app.watcher.Watch(home ? std::wstring() : app.directory);
}

// --- tabs -------------------------------------------------------------------
namespace
{
// Il pannello attivo, sempre. `panes` e' dichiarato "mai vuoto" ma un vettore vuoto
// e' a un `clear` di distanza, e un indice fuori dalla fine qui sarebbe una lettura
// di memoria che sembra valida: meglio un pannello vuoto, che si vede.
Pane& ActivePane(AppState& app)
{
    if (app.panes.empty())
        app.panes.push_back(Pane{});
    if (app.activePane < 0 || app.activePane >= static_cast<int>(app.panes.size()))
        app.activePane = 0;
    return app.panes[static_cast<size_t>(app.activePane)];
}

// Lo scambio vero e proprio, con il tab: i campi vivi vanno nel tab e quelli del tab
// vengono in vita. Usato da `Save`/`Load` e dallo scambio puro, cosi' le tre strade
// non possono divergere su **quali** campi appartengono a un tab.
void SwapLiveTab(AppState& app, Tab& tab)
{
    std::swap(app.directory, tab.directory);
    app.entries.swap(tab.entries);
    std::swap(app.cursor, tab.cursor);
    std::swap(app.selectionAnchor, tab.selectionAnchor);
    app.selection.swap(tab.selection);
    std::swap(app.filter, tab.filter);
    std::swap(app.errorText, tab.errorText);
    std::swap(app.scroll, tab.scroll);
    std::swap(app.filterActive, tab.filterActive);
    std::swap(app.sortColumn, tab.sortColumn);
    std::swap(app.sortAscending, tab.sortAscending);
    std::swap(app.groupMode, tab.groupMode);
    std::swap(app.viewMode, tab.viewMode);
    app.history.swap(tab.history);
    std::swap(app.historyIndex, tab.historyIndex);
}

// La vista che **deriva** quei campi: righe, gruppi, griglia, riga sotto il puntatore.
// Viaggia con lo stato e non e' una comodita': un pannello disegnato con le righe
// dell'altro mostrerebbe i file di un'altra cartella, e un indice di sovrappasso
// preso in prestito illumina una riga che non c'entra. Sono tutti `swap`: nessun
// vettore si copia.
void SwapLiveView(AppState& app, PaneView& view)
{
    app.rows.swap(view.rows);
    app.groups.swap(view.groups);
    std::swap(app.matchCount, view.matchCount);
    std::swap(app.viewDirty, view.viewDirty);
    app.gridRects.swap(view.gridRects);
    std::swap(app.gridRectsRows, view.gridRectsRows);
    std::swap(app.gridRectsWidth, view.gridRectsWidth);
    std::swap(app.gridRectsScale, view.gridRectsScale);
    std::swap(app.gridRectsView, view.gridRectsView);
    std::swap(app.gridRectsGroup, view.gridRectsGroup);
    std::swap(app.gridRectsIcon, view.gridRectsIcon);
    std::swap(app.hovered, view.hovered);
}
} // namespace

void SaveActiveTab(AppState& app)
{
    Pane& pane = ActivePane(app);
    if (pane.activeTab < 0 || pane.activeTab >= static_cast<int>(pane.tabs.size()))
        return;

    Tab& tab = pane.tabs[static_cast<size_t>(pane.activeTab)];
    tab.directory       = app.directory;
    tab.entries         = std::move(app.entries);
    tab.cursor          = app.cursor;
    tab.selectionAnchor = app.selectionAnchor;
    tab.selection       = std::move(app.selection);
    tab.filter          = app.filter;
    tab.errorText       = app.errorText;
    tab.scroll          = app.scroll;
    tab.filterActive    = app.filterActive;
    tab.sortColumn      = app.sortColumn;
    tab.sortAscending   = app.sortAscending;
    tab.groupMode       = app.groupMode;
    tab.viewMode        = app.viewMode;
    tab.history         = app.history;
    tab.historyIndex    = app.historyIndex;
}

void LoadActiveTab(AppState& app)
{
    Pane& pane = ActivePane(app);
    if (pane.activeTab < 0 || pane.activeTab >= static_cast<int>(pane.tabs.size()))
        return;

    Tab& tab = pane.tabs[static_cast<size_t>(pane.activeTab)];
    app.directory       = tab.directory;
    app.entries         = std::move(tab.entries);
    app.cursor          = tab.cursor;
    app.selectionAnchor = tab.selectionAnchor;
    app.selection       = std::move(tab.selection);
    app.filter          = tab.filter;
    app.errorText       = tab.errorText;
    app.scroll          = tab.scroll;
    app.filterActive    = tab.filterActive;
    app.sortColumn      = tab.sortColumn;
    app.sortAscending   = tab.sortAscending;
    app.groupMode       = tab.groupMode;
    app.viewMode        = tab.viewMode;
    app.history         = tab.history;
    app.historyIndex    = tab.historyIndex;
    app.rectSelecting   = false;
    app.viewDirty       = true;

    UpdateWindowTitle(app);
}

void SwapLiveWithPane(AppState& app, int pane)
{
    if (pane < 0 || pane >= static_cast<int>(app.panes.size()))
        return;
    Pane& target = app.panes[static_cast<size_t>(pane)];
    if (target.activeTab < 0 || target.activeTab >= static_cast<int>(target.tabs.size()))
        return;

    SwapLiveTab(app, target.tabs[static_cast<size_t>(target.activeTab)]);
    SwapLiveView(app, target.view);
}

namespace
{
// I campi vivi tornano nel pannello che si lascia e quelli del pannello nuovo vengono
// in vita. Non basta `SwapLiveWithPane`: quello scambia con **un** pannello, e qui i
// pannelli sono due — la vista di quello che si lascia deve andare al suo posto, e
// quella dell'altro deve arrivare in vita, senza mescolarsi.
//
// Non salva: chi chiama ha gia' messo lo stato vivo al suo posto. E' separato perche'
// `SplitPane` salva e poi passa di qui — un secondo `SaveActiveTab` salverebbe un
// elenco che il primo ha gia' spostato nel tab, cioe' il vuoto.
void SwitchToPane(AppState& app, int pane)
{
    // Lo scambio sul pannello che si lascia mette la vista viva al suo posto; in vita
    // resta la vista **vecchia** di quel pannello, che il secondo scambio — sul
    // pannello nuovo — porta a destinazione. Li' e' quello che ci si aspetta: la vista
    // di un pannello inattivo e' la sua ultima, e viene riscritta appena lo si lascia.
    SwapLiveView(app, app.panes[static_cast<size_t>(app.activePane)].view);

    app.activePane = pane;
    LoadActiveTab(app);

    SwapLiveView(app, app.panes[static_cast<size_t>(app.activePane)].view);
    app.stats.CancelQueued();
    app.previews.CancelPending();
}
} // namespace

void ActivatePane(AppState& app, int pane)
{
    if (pane < 0 || pane >= static_cast<int>(app.panes.size()) || pane == app.activePane)
        return;
    SaveActiveTab(app);
    SwitchToPane(app, pane);
}

// Chiude il pannello **attivo**. Cio' che si stava guardando non si butta: lo stato vivo
// va nel suo tab prima di buttarlo (`SaveActiveTab`), e i suoi tab passano in coda a
// quelli del pannello che resta — uno o dieci che siano. Le schede sono schede, il
// pannello e' solo un modo di guardarle: chiuderlo non deve far sparire niente.
void ClosePane(AppState& app)
{
    if (app.panes.size() < 2)
        return;

    SaveActiveTab(app);

    // La vista viva e' quella del pannello che sparisce: si mette da parte e si butta,
    // perche' le sue righe non servono piu' a nessuno — le voci sono gia' nel tab.
    // Lasciarla in vita vorrebbe dire disegnare il pannello che resta con le righe
    // dell'altro.
    PaneView discarded;
    SwapLiveView(app, discarded);

    const int closing = app.activePane;
    std::vector<Tab> moving = std::move(app.panes[static_cast<size_t>(closing)].tabs);
    app.panes.erase(app.panes.begin() + closing);

    // Ne resta uno, e diventa quello attivo: zero e' l'unico indice che c'e'.
    app.activePane = 0;
    Pane& kept = app.panes[0];
    // In coda, non in testa: le schede che c'erano restano dove sono, e chi stava
    // guardando l'altro pannello le ritrova dopo le sue.
    for (Tab& tab : moving)
        kept.tabs.push_back(std::move(tab));

    // Il divisore non c'e' piu': la sua posizione non vuol dire niente, e la prossima
    // divisione riparte dal mezzo.
    app.splitFraction = 0.5f;
    app.splitDrag     = -1;

    // Lo stato vivo deve diventare quello del pannello che resta: la sua scheda accesa —
    // che gli indici non hanno toccato, perche' i tab arrivati stanno in coda — e la sua
    // vista.
    LoadActiveTab(app);
    SwapLiveView(app, kept.view);
    // Le righe rimesse in vita sono quelle dell'ultimo fotogramma in cui quel pannello
    // si e' disegnato, e `LoadActiveTab` ha appena cambiato le voci: si rifa' la vista
    // invece di mostrare indici che non corrispondono.
    app.viewDirty = true;
    app.stats.CancelQueued();
    app.previews.CancelPending();
    // La sorveglianza segue la cartella che resta: era su quella del pannello chiuso.
    if (!IsHome(app.directory))
        app.watcher.Watch(app.directory);
}

// Apre il **secondo** pannello, sulla stessa cartella di quello attivo, e ce lo porta.
// Con due pannelli aperti lo **stesso** asse li richiude (`ClosePane`): la seconda
// pressione del tasto che ha diviso disfa, che e' quello che ci si aspetta da un
// interruttore.
void SplitPane(AppState& app, bool column)
{
    if (app.panes.empty())
        return;

    if (app.panes.size() >= 2)
    {
        if (app.splitColumn == column)
        {
            ClosePane(app);
            return;
        }
        // L'altro asse: la stessa finestra divisa nel verso opposto. Il divisore torna
        // in mezzo, perche' la posizione dell'uno non vuol dire niente per l'altro.
        app.splitColumn   = column;
        app.splitFraction = 0.5f;
        app.viewDirty     = true;
        return;
    }

    // Cio' che il pannello nuovo eredita: la cartella, e **come** la si sta guardando.
    // Una seconda vista della stessa cartella ordinata in un altro modo sarebbe una
    // domanda, non una risposta: chi divide vuole vedere lo stesso elenco due volte.
    const std::wstring directory   = app.directory;
    const SortColumn   sortColumn  = app.sortColumn;
    const bool         sortAscending = app.sortAscending;
    const GroupMode    groupMode   = app.groupMode;
    const ViewMode     viewMode    = app.viewMode;

    SaveActiveTab(app);   // la cartella aperta va nel tab del pannello che c'e' gia'

    Pane pane;
    Tab  tab;
    tab.id            = app.nextTabId++;
    tab.directory     = directory;
    tab.sortColumn    = sortColumn;
    tab.sortAscending = sortAscending;
    tab.groupMode     = groupMode;
    tab.viewMode      = viewMode;
    pane.tabs.push_back(std::move(tab));
    app.panes.push_back(std::move(pane));

    app.splitColumn   = column;
    app.splitFraction = 0.5f;

    // `ActivatePane` salverebbe di nuovo, e salverebbe il vuoto: si passa di qui.
    SwitchToPane(app, static_cast<int>(app.panes.size()) - 1);
    // Il tab del pannello nuovo non ha le voci — un tab appena aperto non le ha mai —
    // e si leggono adesso. `recordHistory` falso: due pannelli sulla stessa cartella
    // non sono due passi da ricordare.
    LoadDirectory(app, directory, /*recordHistory=*/false);
}
// Dove finirebbe il tab trascinato, dato il punto del puntatore. Sta qui e non nel
// gestore del mouse perche' il marcatore disegnato e il rilascio devono leggere la
// stessa risposta: due calcoli che devono coincidere prima o poi non coincidono.
void UpdateTabDrop(AppState& app, float x, float y)
{
    app.tabDropTarget = -1;
    const ClickTarget* hit = HitTest(app, x, y, ClickTarget::Kind::Tab);
    if (hit == nullptr)
        return;
    // Solo le schede del pannello **da cui si e' partiti**: l'albero su cui si agisce e'
    // quello, e un indice preso dalla fila dell'altro sposterebbe un tab che non c'entra.
    // Trascinare una scheda nell'altro pannello e' un'altra tappa.
    if (hit->pane >= 0 && hit->pane != app.activePane)
        return;
    const std::vector<Tab>& tabs = app.panes[static_cast<size_t>(app.activePane)].tabs;
    const int index = hit->index;
    if (index < 0 || index >= static_cast<int>(tabs.size()))
        return;
    if (tabs[static_cast<size_t>(index)].id == app.dragTab)
        return;   // sopra se stesso non si va da nessuna parte

    // Il terzo di mezzo vuol dire "dentro": e' la zona piu' stretta, cosi' i due
    // bordi restano comodi per mettersi prima o dopo.
    const float third = hit->rect.w / 3.0f;
    const float left  = hit->rect.x;
    app.tabDropTarget = index;
    if (x >= left + third && x <= left + third * 2.0f)
    {
        app.tabDropInto = true;
        DiagLog("  tab drop: DENTRO tab " + std::to_string(index) + " (x " +
                std::to_string(x) + ")");
        return;
    }
    app.tabDropInto  = false;
    app.tabDropAfter = (x > left + third);
    DiagLog("  trascinamento di un tab: x " + std::to_string(x) + ", tab " +
            std::to_string(index) + " da " + std::to_string(left) + " a " +
            std::to_string(left + hit->rect.w) + ", terzo " + std::to_string(third) +
            ", esito " + (app.tabDropAfter ? "dopo" : "prima"));
}

void NewTab(AppState& app, const std::wstring& directory)
{
    SaveActiveTab(app);

    Tab tab;
    tab.id = app.nextTabId++;
    // A new tab inherits the view settings, not the location: the user's idea of
    // "how I look at files" should not reset when a tab is opened.
    tab.sortColumn    = app.sortColumn;
    tab.sortAscending = app.sortAscending;
    tab.groupMode     = app.groupMode;
    tab.directory     = directory;

    Pane& pane = app.panes[static_cast<size_t>(app.activePane)];
    pane.tabs.push_back(std::move(tab));
    pane.activeTab = static_cast<int>(pane.tabs.size()) - 1;

    app.entries.clear();
    app.selection.clear();
    app.cursor.clear();
    app.selectionAnchor.clear();
    app.scroll = 0.0f;
    app.filter.clear();
    app.filterActive = false;
    LoadDirectory(app, directory);
}

void ActivateTab(AppState& app, int index)
{
    Pane& pane = app.panes[static_cast<size_t>(app.activePane)];
    if (index < 0 || index >= static_cast<int>(pane.tabs.size()) || index == pane.activeTab)
        return;

    SaveActiveTab(app);
    pane.activeTab = index;
    LoadActiveTab(app);
    app.stats.CancelQueued();
    app.previews.CancelPending();
}

void CloseTab(AppState& app, int index)
{
    Pane& pane = app.panes[static_cast<size_t>(app.activePane)];
    if (index < 0 || index >= static_cast<int>(pane.tabs.size()))
        return;
    if (pane.tabs.size() == 1)
        return;   // the last tab is not closable: use the window button instead

    const bool closingActive = (index == pane.activeTab);
    pane.tabs.erase(pane.tabs.begin() + index);

    if (index < pane.activeTab)
    {
        // The active tab merely shifted down; the live state still matches it.
        --pane.activeTab;
        return;
    }

    if (closingActive)
    {
        if (pane.activeTab >= static_cast<int>(pane.tabs.size()))
            pane.activeTab = static_cast<int>(pane.tabs.size()) - 1;
        LoadActiveTab(app);
        app.stats.CancelQueued();
        app.previews.CancelPending();
    }
}

// Sposta il tab trascinato dove il marcatore dice. L'albero e' quello del pannello
// attivo: il trascinamento di un tab fra pannelli e' un'altra tappa.
void ApplyTabDrop(AppState& app)
{
    std::vector<Tab>& tabs = app.panes[static_cast<size_t>(app.activePane)].tabs;
    int& activeTab = app.panes[static_cast<size_t>(app.activePane)].activeTab;

    const int activeId = tabs[static_cast<size_t>(activeTab)].id;
    const int dragged  = TabById(app, app.dragTab);
    const int target   = app.tabDropTarget;
    const bool into    = app.tabDropInto;
    const bool after   = app.tabDropAfter;
    app.dragTab       = -1;
    app.tabPress      = -1;
    app.tabDropTarget = -1;

    if (dragged < 0 || target < 0 || target >= static_cast<int>(tabs.size()) ||
        target == dragged)
        return;

    if (into)
    {
        // Dentro un altro tab: si diventa suoi figli. Un tab non puo' finire dentro
        // un proprio discendente — sarebbe un anello, e un albero con un anello non
        // e' piu' un albero.
        if (TabIsDescendantOf(app, tabs[static_cast<size_t>(target)].id,
                              tabs[static_cast<size_t>(dragged)].id))
        {
            DiagLog("  tab drop: rifiutato (discendente)");
            return;
        }
        tabs[static_cast<size_t>(dragged)].parentId = tabs[static_cast<size_t>(target)].id;
        DiagLog("  tab drop: " + std::to_string(tabs[static_cast<size_t>(dragged)].id) +
                " figlio di " + std::to_string(tabs[static_cast<size_t>(target)].id));
        return;
    }

    // Prima o dopo: si diventa **fratelli** del bersaglio. Fra fratelli l'ordine e'
    // l'ordine nel vettore, quindi qui il tab si sposta davvero nel vettore — con
    // l'identita' che resta, che e' quello che tiene in piedi i legami.
    Tab moved = tabs[static_cast<size_t>(dragged)];
    moved.parentId = tabs[static_cast<size_t>(target)].parentId;
    tabs.erase(tabs.begin() + dragged);
    int at = (target > dragged) ? target - 1 : target;
    if (after)
        ++at;
    at = (std::max)(0, (std::min)(at, static_cast<int>(tabs.size())));
    tabs.insert(tabs.begin() + at, moved);

    // L'indice attivo segue il **tab** che era attivo, non la sua posizione.
    const int stillActive = TabById(app, activeId);
    if (stillActive >= 0)
        activeTab = stillActive;
}

// Le due chiusure dell'albero. `CloseInTree` chiude il tab **e i suoi figli**;
// `CloseAllInTree` chiude tutto l'albero a cui appartiene, radice compresa.
//
// La lettura e' nostra: l'evidenza sono due nomi di hotkey nel binario
// (`HK_CloseInTree`, `HK_CloseAllInTree`, `docs/03`), e cosa significhino di
// preciso non e' ricostruito. Questa e' la lettura che li distingue l'uno
// dall'altro, ed e' dichiarata.
void CloseTabTree(AppState& app, int id, bool wholeTree)
{
    if (TabById(app, id) < 0)
        return;
    const int root = wholeTree ? TabRootId(app, id) : id;
    const std::vector<Tab>& tabs = app.panes[static_cast<size_t>(app.activePane)].tabs;

    std::vector<int> doomed;
    for (const Tab& tab : tabs)
    {
        if (TabIsDescendantOf(app, tab.id, root))
            doomed.push_back(tab.id);
    }
    // L'ultimo tab non si chiude: resterebbe una finestra senza niente dentro.
    // Si chiude tutto tranne uno, e si dice invece di far finta.
    const bool all = (doomed.size() == tabs.size());
    if (all)
        doomed.pop_back();

    // Dal fondo: chiudendo, gli indici sotto scivolano, e chi resta deve restare
    // valido.
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        CloseTab(app, TabById(app, *it));
}

// Un pannello che non si sta guardando tiene le sue voci **nel tab**
// (`SwapLiveWithPane` le scambia al disegno), quindi una cartella cambiata da fuori
// resta com'era finche' non la si guarda: e' quello che si vuole per una modifica
// qualsiasi, ed e' sbagliato per un'operazione partita da qui. Dopo una caduta
// sull'altro pannello i file devono **arrivare** sotto gli occhi, non comparire al
// primo clic.
void ReloadPaneShowing(AppState& app, const std::wstring& directory)
{
    if (directory.empty())
        return;

    for (int pane = 0; pane < static_cast<int>(app.panes.size()); ++pane)
    {
        if (pane == app.activePane)
            continue;   // quello attivo lo ricarica chi esegue l'operazione
        Pane& data = app.panes[static_cast<size_t>(pane)];
        if (data.activeTab < 0 || data.activeTab >= static_cast<int>(data.tabs.size()))
            continue;
        Tab& tab = data.tabs[static_cast<size_t>(data.activeTab)];
        if (tab.directory.empty() || _wcsicmp(tab.directory.c_str(), directory.c_str()) != 0)
            continue;

        std::string error;
        tab.entries = ReadDirectory(tab.directory, &error, app.longPaths);
        // La vista si rifa' mentre quel pannello e' in vita, cioe' al prossimo
        // fotogramma: e' il suo `viewDirty` che lo dice.
        data.view.viewDirty = true;
    }
}

// --- navigation, recents and bookmarks -------------------------------------- 
void NavigateBack(AppState& app)
{
    if (app.historyIndex <= 0)
        return;
    --app.historyIndex;
    LoadDirectory(app, app.history[static_cast<size_t>(app.historyIndex)], false);
}

void NavigateForward(AppState& app)
{
    if (app.historyIndex + 1 >= static_cast<int>(app.history.size()))
        return;
    ++app.historyIndex;
    LoadDirectory(app, app.history[static_cast<size_t>(app.historyIndex)], false);
}

// Le azioni che il **menu contestuale** chiama: eseguire un comando, aprire la voce
// scelta, ricaricare la cartella tenendo il posto, mostrare le proprieta', crearne una
// nuova. Stanno qui perche' il menu e' gia' un modulo e i comandi no: quando ci sara'
// `commands.*` escono da questo blocco, e il confine si stringe.

// Ricarica la cartella mantenendo la selezione sui nomi che esistono ancora.
//
// LoadDirectory svuota la selezione e sceglie la prima riga, ed e' giusto perche'
// cambia cartella. Dopo un incolla o un'eliminazione no: l'utente sta lavorando
// li' e perdere il posto a ogni operazione lo costringe a ricercarlo.
void ReloadKeeping(AppState& app, const std::unordered_set<std::wstring>& keep,
                   const std::wstring& preferCursor, float restoreScroll)
{
    // Le due cose da ritrovare si copiano **prima** di ricaricare, e non e' una
    // precauzione: chi chiama passa quasi sempre `app.selection` e `app.cursor`, cioe'
    // esattamente i campi che `LoadDirectory` azzera. Per riferimento, qui dentro
    // restava il vuoto appena fatto: la selezione tornava alla prima riga a ogni
    // cambiamento della cartella, e nessuno se ne accorgeva finche' non si lavorava
    // **durante** un'operazione (`docs/02` §7.70).
    const std::unordered_set<std::wstring> keepNames = keep;
    const std::wstring prefer = preferCursor;

    LoadDirectory(app, app.directory, /*recordHistory=*/false);

    const auto present = [&](const std::wstring& name) {
        for (const Entry& entry : app.entries)
            if (entry.wideName == name)
                return true;
        return false;
    };

    app.selection.clear();
    for (const Entry& entry : app.entries)
        if (keepNames.count(entry.wideName) != 0)
            app.selection.insert(entry.wideName);

    if (!prefer.empty() && present(prefer))
    {
        app.cursor = prefer;
        if (app.selection.empty())
            app.selection.insert(prefer);
        app.selectionAnchor = prefer;
    }
    else
    {
        // Il primo nome sopravvissuto in ordine di lista, non un elemento
        // qualunque dell'insieme: la selezione e' un insieme non ordinato.
        for (const Entry& entry : app.entries)
        {
            if (app.selection.count(entry.wideName) != 0)
            {
                app.cursor          = entry.wideName;
                app.selectionAnchor = entry.wideName;
                break;
            }
        }
    }

    app.scroll = restoreScroll;
    app.viewDirty = true;

    // La cartella e' cambiata, quindi le statistiche che la riguardano non
    // valgono piu'. Serve invalidare QUESTA cartella, non quella che si sta
    // guardando: i numeri di una sottocartella si vedono dal genitore, quindi e'
    // li' che si noterebbe che sono vecchi.
    app.stats.Invalidate(app.directory);
}
} // namespace Bolt
