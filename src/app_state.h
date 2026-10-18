// ---------------------------------------------------------------------------
// Lo stato dell'applicazione e i tipi che attraversano piu' di un modulo.
//
// Questo file esiste per una ragione sola: `main.cpp` e' arrivato a **undicimila
// righe** e ogni cosa ci viveva dentro, nello stesso namespace anonimo — quindi
// niente di quella roba si poteva usare da un altro file. Il confine non c'era, e
// non si poteva spostare niente senza crearlo.
//
// Qui ci sta quello che serve a **piu' di un modulo**: `AppState` con lo stato
// della finestra, e i tipi che lo compongono (una voce, un tab, un menu, una
// colonna, una cella). Non ci sta niente che riguardi il *disegno*: chi disegna
// include questo file, non il contrario, cosi' la direzione delle dipendenze e'
// una sola.
//
// `AppState` e' grosso, ed e' voluto: e' lo stato di una finestra sola, non di un
// servizio, ed e' la struttura su cui il riferimento tiene tutto (`docs/02`). Il
// difetto non era la sua dimensione, era che nessuno potesse vederlo da fuori.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dir_stats.h"
#include "dir_watch.h"
#include "file_ops.h"
#include "font_atlas.h"
#include "image_preview.h"
#include "keymap.h"
#include "language.h"
#include "ntfs_attributes.h"
#include "shell_icons.h"
#include "shell_preview.h"
#include "shell_location.h"
#include "text_view.h"
#include "theme.h"
#include "ui_renderer.h"

namespace Bolt
{
constexpr int kGlyphLayer      = 0;
constexpr int kIconLayer       = 1;
constexpr int kPreviewLayer    = 2;
constexpr int kSmallGlyphLayer = 3;
// Un testo si legge in un carattere **a spaziatura fissa**: in un carattere
// proporzionale le colonne non si allineano, e quello che si guarda in un
// visualizzatore e' spesso proprio l'allineamento. E' il quinto strato: i primi
// quattro non bastavano.
constexpr int kMonoLayer = 4;
// Un'immagine intera, per l'anteprima grande dell'inspector: la miniatura della
// shell vive in una cella da 128 px e ingrandirla mostrerebbe 128 px allargati. E'
// un sesto strato dell'array, cosi' si disegna con lo stesso shader delle altre cose
// e non serve ne' un secondo campionatore ne' un ramo nel frammento.
constexpr int kBigImageLayer = 5;
constexpr int kBigImageSide  = 1024;
// Il lato di ogni strato dell'array. E' una costante di tutto il file e non di
// `wWinMain` perche' `ApplyFonts` — che rifa' l'atlante quando cambia il
// carattere scelto nelle opzioni — la usa da fuori.
constexpr int kTextureLayerSize = 1024;

// --- theme -----------------------------------------------------------------
//
// I ruoli di colore e le due tavolozze stanno in `theme.h`, perche' il tema non e'
// solo il colore di uno stato: e' una cosa che si sceglie, si ricorda e si puo'
// cambiare a finestra aperta. Qui c'e' quale delle due si sta usando.

// --- model -----------------------------------------------------------------
enum class SortColumn { Name, Type, Modified, Folders, Files, Size };

struct Entry
{
    std::wstring wideName;
    std::string  name;
    std::string  extension;     // lowercase, no leading dot
    std::string  typeText;
    std::string  sizeText;
    std::string  modifiedText;    // compact, for the list column
    std::string  modifiedFull;    // with time, for the inspector
    uint64_t     bytes = 0;
    bool         isDirectory = false;
    FILETIME     modified{};
    FILETIME     created{};      // creazione e ultimo accesso: le da' FindFirstFile,
    FILETIME     accessed{};     // e servono all'inspector come la modifica
    bool         readable = true;

    // Il nome assoluto, quando la voce viene da una cartella che **non e' un
    // percorso** (il cestino): li' il nome da solo non basta ne' ad aprirla ne' a
    // chiedere alla shell di ripristinarla. Vuoto per le voci normali, che il
    // percorso ce l'hanno gia' dalla cartella aperta.
    std::wstring location;

    // Il PIDL, per le stesse voci: e' l'identita' che la shell riconosce. Il nome
    // di un file nel cestino e' il percorso del file dentro `$Recycle.Bin`, che e'
    // un'altra voce — chiederle il menu di quel nome da' il menu di *quel* file, e
    // "Ripristina" sparisce.
    std::vector<BYTE> pidl;

    // Vero se aprirla porta dove dice il suo nome. Le voci normali si'; un'unita'
    // dentro "Questo PC" si'; una cartella dentro il cestino no: li' il nome e' il
    // percorso del file riciclato, e aprirlo mostrerebbe il blob, non la cartella.
    bool navigable = true;

    // Vero se la voce **e'** la radice di un volume. Si decide quando si apre la
    // cartella di spazio dei nomi, non a ogni fotogramma: chiedere il tipo di
    // un'unita' puo' far girare un lettore ottico, e una lista si ridisegna
    // sessanta volte al secondo.
    bool volumeRoot = false;
};

// Una voce dell'anteprima di una cartella: il minimo per disegnarne una cella. Non
// e' un `Entry` perche' non e' una riga di lista: non si ordina, non si seleziona,
// non si apre — si guarda.
struct PeekEntry
{
    std::wstring path;         // il percorso completo, per chiedere alla shell l'anteprima
    std::string  name;         // il nome, da scrivere sotto la cella
    std::string  extension;    // minuscola, senza punto; vuota per le cartelle
    bool         isDirectory = false;
};

struct Place
{
    std::string  label;
    std::wstring path;

    // **Quale oggetto della shell e'**: un percorso per un'unita', un nome di
    // spazio dei nomi (`::{...}`) per una cartella nota o per il cestino. E' quello
    // che si passa alla shell per avere l'icona **sua** — un disco per `C:`, non una
    // cartella — e non sempre coincide con `path`: `path` dice dove si va, questo
    // dice da chi farsi disegnare.
    std::wstring icon;

    // Per i volumi: quanto e' pieno, e quanto resta in lettere. Vuoti per tutto
    // il resto — una cartella non ha un'occupazione da mostrare.
    float       usedFraction = 0.0f;
    std::string freeText;

    // **Nostra**, non della shell: e' una riga che ha fissato l'utente con
    // `Fissa in...`, e nell'accesso rapido sta accanto a quelle che ci ha messo
    // Windows. Si disegna con un segno suo, perche' e' l'unica delle due che si puo'
    // togliere da qui — l'altra la conosce solo il jump list della shell.
    bool        ours = false;
};

// Rectangles recorded while drawing, so input handling can hit test them without
// duplicating the layout maths. Rebuilt every frame.
struct ClickTarget
{
    enum class Kind
    {
        Column, ColumnEdge, ColumnFilter, ColumnFilterRow, Breadcrumb, NavButton, Place,
        SidebarHeader, Row, GroupHeader, GroupMode, Checkbox,
        Menu, Tab, TabClose, TabNew,
        WindowMinimize, WindowMaximize, WindowClose,
        MenuRow, MenuHeader,
        // I selettori di vista e lo stepper delle anteprime, nella barra in fondo.
        ViewSelect, PreviewDown, PreviewUp,
    };
    Rect         rect;
    Kind         kind = Kind::Row;
    int          index = 0;      // column index or row index
    std::wstring path;           // for breadcrumbs and places
    std::string  label;          // for group headers
};

// Una riga di proprieta': etichetta a sinistra, valore a destra. Le stesse righe
// servono all'inspector e al pannello delle proprieta': sono due viste della
// stessa cosa, e costruirle in due posti sarebbe il modo di farle divergere.
struct InspectorRow
{
    std::string label;
    std::string value;
    bool        growing = false;   // il numero sta ancora crescendo: si sottolinea
    bool        tail    = false;   // un percorso si taglia davanti, non dietro
};

// --- view model -------------------------------------------------------------
// The list is not the directory. A filter removes entries and a grouping inserts
// headers between them, so what gets drawn and hit tested is a list of rows over
// the entry array, rebuilt only when something that affects it changes.

enum class GroupMode { None, Name, Date, Size, Count, Volume };

// How the list is laid out. Details and List are rows of text; Icons and Tiles
// are a grid of cells built around a thumbnail. Two row layouts and two cell
// sizes cover what Explorer exposes as its eight view commands, because most of
// those are the same layout at a different size.
enum class ViewMode { Details, List, Tiles, Icons };

// Geometry of the current view. |columns| is only meaningful in a grid.
struct ViewMetrics
{
    bool  grid       = false;
    float rowHeight  = 24.0f;
    float iconSize   = 16.0f;
    float cellWidth  = 0.0f;
    float cellHeight = 0.0f;
    float bandHeight = 0.0f;
    float padding    = 4.0f;
    int   columns    = 1;
};

struct Row
{
    bool header = false;
    int  entry  = -1;   // index into AppState::entries
    int  group  = -1;   // index into AppState::groups
};

struct Group
{
    std::string label;
    int         count = 0;
};

// --- command palette ---------------------------------------------------------
// A command is data, not a callback: the table below is the single place a
// command is declared, and |RunCommand| is the single place it is executed. The
// reference exposes 319 of them with a tooltip each; this is the subset the shell
// can actually carry out, which is the honest place to start.
// --- lingua ------------------------------------------------------------------
//
// Quale delle due lingue si mostra lo decide `language.h/.cpp`: la scelta vale per
// tutta l'applicazione, e anche `file_ops.cpp` ha i suoi messaggi. Le coppie di
// stringhe restano qui, accanto a chi le usa.

enum CommandId
{
    CmdNewTab, CmdCloseTab, CmdNextTab, CmdPrevTab,
    CmdGoUp, CmdBack, CmdForward, CmdRefresh, CmdOpenExplorer, CmdOpenConsole, CmdRecalcStats,
    CmdGoTo,
    CmdToggleSidebar, CmdZoomIn, CmdZoomOut, CmdZoomReset, CmdToggleDetails,
    CmdTextWhitespace, CmdTextLineNumbers, CmdTextMinimap, CmdTextWrap, CmdTextZoomIn,
    CmdTextZoomOut, CmdImageZoomIn, CmdImageZoomOut, CmdImageZoomFit, CmdToggleImageGrid,
    CmdViewDetails, CmdViewList, CmdViewTiles, CmdViewIcons,
    CmdSortName, CmdSortType, CmdSortModified, CmdSortFolders, CmdSortFiles,
    CmdSortSize, CmdSortDirection,
    CmdGroupNone, CmdGroupName, CmdGroupDate, CmdGroupSize, CmdGroupCount, CmdGroupVolume,
    CmdFocusFilter, CmdClearFilter, CmdScrollTop, CmdScrollBottom,
    CmdToggleHoverToSelect, CmdToggleUnderlineOnHover, CmdToggleTooltips,
    CmdSingleClick, CmdSingleClickFoldersOnly, CmdActOnPress, CmdDoubleClickToParent,
    CmdShowCheckboxes,
    CmdCloseInTree, CmdCloseAllInTree, CmdDetachTab,
    CmdLanguageItalian, CmdLanguageEnglish, CmdShowSettingsPath,
    // L'aspetto: le due tavolozze e "segui il sistema". Sono le chiavi
    // `ColorSchemeLight`, `ColorSchemeDark` e `SystemColorScheme` dello spec.
    CmdColorSchemeLight, CmdColorSchemeDark, CmdSystemColorScheme,
    CmdToggleBookmark, CmdClearBookmarks, CmdClearRecents,
    // Operazioni su file (M7).
    CmdCopy, CmdCut, CmdPaste, CmdDelete, CmdDeletePermanent, CmdRename, CmdNewFolder,
    // M9: il pannello delle opzioni, raggiungibile anche dalla palette.
    CmdOpenOptions,
    // L'editor della mappa dei tasti. Un comando come gli altri — sta nella palette
    // e si puo' rimappare lui stesso — ma **senza tasto di partenza**: finche' non
    // gliene si da' uno, si arriva dalla sua riga fra le opzioni.
    CmdEditKeyMap,
    // Il fissaggio: due domande — dove, e da dove togliere — sulla stessa lista di
    // destinazioni. Sono due comandi e non uno con un verso, perche' chi cerca
    // "togli" non deve leggere "fissa" e capire da solo che fa il contrario.
    CmdPinTo, CmdUnpin,
    // Le proprieta': quella in-app e quella di Windows, due voci distinte.
    CmdProperties, CmdPropertiesWindows,
    // La tavolozza dei comandi. Era l'unica cosa che si apriva da un tasto
    // **senza essere un comando**: adesso lo e', e il tasto sta nella mappa come
    // tutti gli altri.
    CmdPalette,
};

// Item ids below this are CommandId values; above it they are indexes into
// AppState::places. The palette has two kinds of row and the id has to say which.
constexpr int kPalettePlaceBase = 1000;

// The reusable part: a modal, filtered, grouped and collapsible list. It knows
// nothing about what an item means — |id| is the caller's business — which is
// what lets the same component serve the command palette and the GoTo window.
// It became a component when the second caller arrived, not before.
//
// M9 aggiunge i **controlli tipizzati**: una riga del pannello delle opzioni non
// e' solo una cosa da scegliere, e' una preferenza con il suo comando. Il tipo
// sta qui e non in un `if` sul chiamante perche' disegno, clic e tastiera devono
// leggere la stessa parola.
enum class OptionControl
{
    None,     // riga di solo testo
    Toggle,   // interruttore
    Stepper,  // valore numerico con − e +
    Slider,   // valore continuo
    Text,     // campo di testo modificabile
    Choice,   // pulsante con freccia: apre un elenco di valori
    // Un pulsante che apre qualcos'altro. Non e' una preferenza: non ha un valore
    // da leggere, e il suo id dice **cosa apre**, non cosa vale.
    Action,
};

struct GroupMenuItem
{
    std::string group;
    std::string label;
    std::string detail;   // shown right aligned in detailed mode
    int         id = 0;   // meaningful only to the caller

    // --- M9: il controllo dentro la riga -------------------------------------
    // `description` e' il *perche'* accanto alla scelta: il riferimento lo mette
    // sotto il nome, non nella documentazione.
    OptionControl control = OptionControl::None;
    std::string   description;
    std::string   valueText;      // Stepper / Text / Choice: cosa mostra il controllo
    bool          toggleOn  = false;
    float         fraction  = 0.0f;   // Slider, 0..1
    // Un'opzione che non si puo' applicare si mostra **spenta**, non sparisce:
    // e' la lezione del riferimento (`System Light / Dark Scheme` li' e' cosi').
    bool          enabled   = true;
    std::vector<std::pair<std::string, int>> choices;   // Choice: (etichetta, valore)
};

struct GroupMenuRow
{
    bool header = false;
    int  group  = -1;   // index into GroupMenu::groups
    int  item   = -1;   // index into GroupMenu::items
};

enum class MenuAction { None, Activate, Close };

struct GroupMenu
{
    bool         open      = false;
    bool         detailed  = false;
    bool         dirty     = true;
    // Il pannello delle opzioni: righe a due linee (nome e descrizione) con il
    // controllo a destra, alte una misura fissa che **non** segue `Spacing`.
    bool         richRows  = false;
    // Con `detailed`, la colonna di destra e' `item.detail`; quando manca, il
    // ripiego e' il **gruppo**, ma solo per i menu che lo chiedono. La palette lo
    // chiede: li' il gruppo e' il contesto di una voce, e sta bene accanto. L'editor
    // dei tasti no: li' quella colonna vuol dire una cosa sola — il tasto — e un
    // gruppo scritto dov'era il tasto sarebbe una bugia su un comando senza tasti.
    bool         detailFallsBackToGroup = false;
    // Dove appoggiarlo. Vuoto: al centro, come la palette. Riempito: sotto
    // quell'angolo, come il pannello dei valori di una colonna, che deve stare
    // attaccato all'intestazione da cui e' nato.
    Rect         anchor{};
    bool         anchored  = false;
    std::wstring filter;
    std::string  title;
    std::string  placeholder;
    std::string  hint;   // the footer line, which differs per caller

    std::vector<GroupMenuItem>      items;
    std::vector<GroupMenuRow>       rows;
    std::vector<std::string>        groups;
    std::unordered_set<std::string> collapsed;
    int          selected   = 0;
    int          matchCount = 0;
};

// One panel's worth of state. Switching tabs saves the live state into the tab
// it came from and loads the target tab's, which keeps every other part of the
// program working on a single set of fields instead of having to know that tabs
// exist. Completed preview and statistic results are global caches keyed by path,
// so they survive a switch for free.
struct Tab
{
    // Un tab ha un'**identita'** e un genitore, non una posizione. Gli indici
    // scivolano appena se ne chiude uno, e un albero tenuto per indici si sbaglia
    // da solo: il figlio del tab 3 diventa il figlio di un altro.
    int          id       = 0;
    int          parentId = -1;   // -1: e' una radice
    std::wstring directory;
    std::vector<Entry> entries;
    std::wstring cursor;
    std::wstring selectionAnchor;
    std::unordered_set<std::wstring> selection;
    std::wstring filter;
    std::string  errorText;
    float        scroll = 0.0f;
    bool         filterActive = false;
    SortColumn   sortColumn = SortColumn::Name;
    bool         sortAscending = true;
    GroupMode    groupMode = GroupMode::None;
    ViewMode     viewMode  = ViewMode::Details;

    // Per-tab navigation history, walked by Alt+Left and Alt+Right.
    std::vector<std::wstring> history;
    int                       historyIndex = -1;
};

// --- menu contestuale: strutture --------------------------------------------
//
// Non e' un GroupMenu, ed e' una scelta e non una svista. Condividono l'idea di
// "una lista di righe etichettate", ma differiscono in tutto il resto: questo si
// ancora al puntatore invece di stare al centro, e' alto quanto il contenuto
// invece di 420 px fissi, ha separatori e non bande, e non ha filtro ne' piede.
// Forzarlo dentro GroupMenu avrebbe voluto dire un campo modalita' e due
// impaginazioni dentro lo stesso componente.

// Id che non sono comandi della palette: il primo apre la selezione, che l'Invio
// gia' fa, il secondo apre la finestra delle proprieta' di Windows.
constexpr int kContextOpen       = -1;
constexpr int kContextProperties        = -2;
constexpr int kContextPropertiesWindows = -3;

struct ContextMenuItem
{
    std::string label;
    // CommandId, kContext*, oppure un id della banda della shell: vedi
    // kShellCmdFirst. La banda e' cosa distingue una voce nostra da una che
    // appartiene alla shell e va eseguita da lei.
    int         command   = 0;
    bool        separator = false;
    // Una voce che esiste ma non si puo' eseguire adesso: la shell offre voci
    // spente per una selezione mista o per un file che non sa gestire. Va
    // mostrata spenta, non nascosta, perche' la sua presenza spiega cosa la
    // shell saprebbe fare in un altro contesto.
    bool        disabled  = false;

    // Sottomenu. `children` vuoto non vuol dire "niente sottomenu": la shell lo
    // descrive con un HMENU che si legge **quando si apre**, perche' il suo
    // contenuto non esiste prima di WM_INITMENUPOPUP. Vedi `HasSubmenu`.
    std::vector<ContextMenuItem> children;
    HMENU shellSubmenu  = nullptr;   // il sottomenu della shell, non ancora letto
    int   shellPosition = 0;         // dove sta la voce nel menu della shell
    // Un sottomenu nostro che si riempie alla prima apertura: i modelli di
    // "Nuovo file" stanno nel registro, e leggerli costa qualche centinaio di
    // aperture di chiave che non si devono pagare a ogni tasto destro.
    bool  lazyNewFile   = false;
    // E cosi' "Invia a": le destinazioni stanno in una cartella dell'utente, e
    // leggerle a ogni tasto destro e' lavoro che si fa quando il menu si apre.
    bool  lazySendTo    = false;
};

// Scelta e finestra di un livello del menu. Ce n'e' una per livello aperto.
struct ContextMenuLevel
{
    int selected  = -1;
    int firstItem = 0;
};

struct ContextMenu
{
    bool open = false;
    float x = 0.0f, y = 0.0f;       // dove e' stato aperto, in coordinate cliente
    // I sottomenu aperti, un indice per livello: openPath[i] e' la voce aperta
    // nel livello i. `levels` ne ha sempre uno in piu' di openPath, e non e' mai
    // vuoto: il livello 0 e' il menu stesso.
    std::vector<int>              openPath;
    std::vector<ContextMenuLevel> levels;
    std::string subtitle;
    std::vector<ContextMenuItem> items;      // le voci del livello 0

    // La shell descrive il proprio menu in un HMENU che non mostriamo mai: serve
    // per leggere le voci, e per i sottomenu anche per farsele popolare. Va
    // tenuto vivo finche' l'IContextMenu e' vivo, e distrutto insieme a lui, o
    // l'HMENU resta appeso a ogni tasto destro.
    HMENU         shellMenu = nullptr;
    IContextMenu* shell     = nullptr;
    // Il menu e' stato chiesto per il fondo della cartella invece che per una
    // selezione: serve a filtrare i verbi quando si legge un sottomenu, perche'
    // allora la lettura avviene molto dopo averlo deciso.
    bool shellIsBackground = false;
};

// Le proprieta' che il file system non dice. Le dimensioni di un'immagine stanno
// nell'intestazione del file e le sa il gestore di quel tipo: e' quello che
// risponde `IPropertyStore`, ed e' la stessa fonte da cui Windows riempie le
// colonne. Si legge una volta per percorso e si tiene.
struct FileProperties
{
    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
};

// Una riga **come si vede**: una riga di testo, o un pezzo di essa quando l'andare
// a capo e' acceso. `offset` e' la posizione nel testo della riga logica, e vale
// zero solo sulla prima: e' cosi' che il numero di riga sa dove scriversi.
struct TextRow
{
    size_t line = 0;
    size_t offset = 0;
    size_t length = 0;
};

// Le colonne della vista a dettagli. `fraction` e' la distanza del bordo sinistro
// dalla larghezza dell'elenco: e' una frazione e non una misura in pixel perche'
// cosi' la vista si stringe e si allarga senza che le colonne si accavallino.
//
// La frazione si puo' cambiare — si trascina il bordo, come in Esplora risorse — e
// quindi il valore vivo sta in `AppState`, non qui: qui c'e' solo quello di
// partenza, che e' anche quello a cui torna un doppio clic sul bordo.
struct ColumnDef
{
    const char* label;     // il nome della chiave dello spec ("Name", "Size", …)
    const char* labelIt;   // come si legge quando la lingua e' l'italiano
    SortColumn  sort;
    float       fraction;   // of the list width, measured from its left edge
    bool        filterable; // se questa colonna offre il filtro per valori
};

// Name, then the columns a directory entry already knows, then the three that
// depend on the recursive statistics and arrive progressively.
const ColumnDef kColumns[] = {
    // Il nome ha il filtro come le altre colonne, ma il suo non e' un elenco di
    // valori — ogni nome e' diverso. La sua linguetta accende la casella di
    // **testo** in fondo, che e' il filtro del nome che serve davvero.
    {"Name",     "Nome",           SortColumn::Name,     0.00f, true},
    {"Type",     "Tipo",           SortColumn::Type,     0.28f, true},
    {"Modified", "Data modifica",  SortColumn::Modified, 0.42f, true},
    // `Folders` e `Files` avevano il titolo appena piu' largo della colonna, e
    // l'imbuto del filtro si mangiava la coda: con il carattere di partenza si
    // sovrapponevano di pochi pixel, con un carattere piu' largo si leggeva
    // `ModifedFoldersFilesSize`. Qui le due colonne hanno lo spazio per il titolo
    // **e** per l'imbuto; se la finestra si stringe, il titolo si accorcia (vedi
    // `DrawHeader`) invece di finire sopra quello vicino.
    //
    // La frazione e' il **bordo sinistro** della colonna, quindi la larghezza e' la
    // differenza fra questa e la prossima: e i titoli italiani sono piu' lunghi di
    // quelli inglesi ("Dimensione" contro "Size"), per cui le misure sono quelle
    // che fanno entrare il titolo **piu' lungo delle due lingue**, non quello corto.
    {"Folders",  "Cartelle",       SortColumn::Folders,  0.60f, true},
    {"Files",    "File",           SortColumn::Files,    0.735f, true},
    {"Size",     "Dimensione",     SortColumn::Size,     0.825f, true},
};
constexpr int kColumnCount = static_cast<int>(sizeof(kColumns) / sizeof(kColumns[0]));

// Il titolo di una colonna si legge in due lingue, e la chiave inglese resta
// quella dello spec: e' con quella che la tabella delle 23 colonne e' scritta, e
// non si rinomina per farla leggere a un italiano.
inline std::string ColumnLabel(int index)
{
    return Tr(kColumns[index].labelIt, kColumns[index].label);
}

// Quanto e' larga una colonna quando la si trascina: sotto questa misura le
// intestazioni non si leggono, e trascinare oltre vorrebbe dire cancellare una
// colonna invece di stringerla.
constexpr float kMinColumnWidth = 44.0f;

// Da dove e' partita l'operazione su file in corso. Serve perche' un comando e una
// caduta raccontano l'esito in modo diverso — "Incollato N elementi" contro
// "Copiato N elementi in <cartella>" — e cambiano cose diverse: l'incolla consuma la
// clipboard, la caduta dichiara subito un esito che altrimenti racconterebbe
// l'origine.
enum class OperationSource
{
    None,
    Command,   // da un comando: incolla, elimina
    Drop,      // da una caduta: copia, spostamento, cestino
};

struct AppState
{
    // Il backend di disegno, dietro l'interfaccia: OpenGL o Direct3D 11. Lo
    // possiede `wWinMain`, che lo sceglie e lo distrugge alla fine.
    IRenderer*       renderer = nullptr;
    FontAtlas       font;
    // Testo secondario: intestazioni, etichette, barra di stato. Rasterizzato
    // alla sua dimensione invece che ricampionato dal corpo.
    FontAtlas       smallFont;

    // Il carattere del testo del visualizzatore, e se e' stato costruito: senza, si
    // usa quello secondario.
    FontAtlas       monoFont;
    bool            monoFontOk = false;
    ShellIconAtlas  icons;
    ShellPreviewCache previews;
    DirStatsService   stats;

    // La tavolozza in uso, e la scelta che l'ha decisa. `systemColorScheme` acceso
    // vuol dire "segui il sistema" — la chiave `SystemColorScheme` del riferimento —
    // e **vince** su `colorScheme`: e' quello che si rilegge quando il sistema
    // chiarisce o scurisce mentre l'applicazione e' aperta.
    Theme             theme;
    ColorScheme       colorScheme       = ColorScheme::Dark;
    bool              systemColorScheme = false;

    HWND          window = nullptr;

    std::vector<Entry> entries;
    std::vector<Place> places;
    std::vector<ClickTarget> clickTargets;

    // --- barra laterale ------------------------------------------------------
    //
    // Quattro elenchi, non uno: i fissaggi della shell, i segnalibri (che sono
    // nostri e stanno in memoria sola, vedi `docs/06`), i volumi e le cartelle
    // standard. Sono cose di natura diversa e si mostrano in sezioni diverse; la
    // barra laterale le disegna in quest'ordine.
    // Chi dice che una cartella e' cambiata. Uno solo, e sorveglia la cartella
    // aperta: vedi `dir_watch.h` per il limite dichiarato.
    DirectoryWatcher watcher;

    std::vector<Place> quickAccess;
    std::vector<Place> volumes;
    // I fissaggi **nostri** di accesso rapido: le cartelle che l'utente ha fissato
    // con `Fissa in...`. Stanno separati da `quickAccess` perche' quello e' della
    // shell e non si tocca — il jump list di Windows non ha un'API per aggiungere una
    // voce, e l'unico modo di toccarlo scriverebbe un file **della shell** (`docs/04`
    // M6, decisione (a)). Vivono in memoria, per la sessione: sono cartelle di
    // qualcuno, e `settings.ini` non ne contiene (`docs/06`).
    std::vector<std::wstring> pins;
    std::unordered_set<std::string> collapsedSections;

    // Dove punta un collegamento, per percorso. Si risolve una volta per file: la
    // risoluzione apre il collegamento, e una riga si disegna sessanta volte al
    // secondo.
    std::unordered_map<std::wstring, std::wstring> shortcutTargets;

    // Le proprieta' che il file system non dice (le dimensioni di un'immagine, per
    // esempio): anche queste si leggono una volta per percorso.
    std::unordered_map<std::wstring, FileProperties> fileProperties;

    // Gli attributi NTFS di un file, per percorso. La lettura apre il volume raw e
    // costa un `DeviceIoControl`: una volta per voce scelta, non una volta per
    // fotogramma — e anche il **motivo** per cui non ci sono si tiene, altrimenti
    // senza privilegi si riproverebbe a ogni ridisegno.
    std::unordered_map<std::wstring, NtfsAttributes::Result> ntfsAttributes;

    // --- visualizzatore di testo (M8) ---------------------------------------
    //
    // Il documento si legge quando cambia la voce scelta e si tiene finche' quella
    // voce resta scelta. E' l'unica cosa in memoria che riguarda il *contenuto* di un
    // file: vive quanto la selezione, e non va da nessuna parte (`docs/06`).
    TextView::Document textDocument;
    std::wstring       textDocumentPath;
    float              textScroll = 0.0f;
    float              textZoom   = 1.0f;

    // Le opzioni, con i nomi della tabella delle chiavi dell'originale.
    bool textShowWhitespace = false;   // InspectorShowWhitespace
    bool textLineNumbers    = true;    // InspectorHideLineNumbers, al contrario
    bool textMinimap        = true;    // InspectorHideMinimap, al contrario
    bool textWrap           = false;   // InspectorWordWrap
    // La griglia dietro l'anteprima **immagine**: serve a distinguere lo sfondo
    // dell'immagine (spesso bianco o trasparente) da quello del pannello. La chiave
    // dell'originale e' `InspectorHideGrid`, e il suo comando e'
    // `HK_InspectorToggleImageGrid` — «Toggle display of image grid to better
    // differentiate background from the image contents» (`analysis/out`).
    bool inspectorHideGrid  = false;   // InspectorHideGrid

    // Le righe come si vedono, e con che misura sono state calcolate: si rifanno solo
    // quando la misura cambia, perche' su un file lungo rifarle a ogni fotogramma
    // sarebbe lavoro sprecato.
    // Dove sta il visualizzatore, in coordinate cliente: la rotella deve sapere se il
    // puntatore e' sopra il testo o sopra la lista, e il rettangolo lo conosce solo il
    // disegno.
    Rect  textViewerRect{};

    // --- anteprima grande (M8) ----------------------------------------------
    //
    // L'immagine decodificata per la voce scelta, caricata nello strato suo. Si
    // ricarica solo quando cambia la voce, e si tiene in memoria quanto la selezione.
    ImagePreview::Image bigImage;
    std::wstring        bigImagePath;
    bool                bigImageUploaded = false;
    float               imageZoom = 1.0f;   // 1 = adattata alla larghezza del pannello
    std::vector<TextRow> textRows;
    bool  textRowsDirty = true;
    float textRowsWidth = 0.0f;
    float textRowsZoom  = 0.0f;
    size_t textLongest  = 0;           // la riga piu' lunga, per la minimap

    // --- l'anteprima di quello che c'e' dentro una cartella scelta -----------
    //
    // Le prime voci della cartella, con il solo necessario a disegnarne una cella.
    // Si leggono **quando la cartella scelta cambia**, non a ogni fotogramma: la
    // lettura tocca il disco, e l'ispettore si ridisegna sessanta volte al secondo.
    std::vector<PeekEntry> peekEntries;
    std::wstring           peekFolder;      // a quale cartella appartengono ("" = nessuna)
    bool                   peekReadable = false;   // la cartella si e' potuta aprire
    bool                   peekMore = false;   // ce n'erano altre oltre quelle lette

    std::vector<Tab> tabs;
    int              activeTab = 0;
    int              nextTabId = 1;

    // Il trascinamento di un tab: quale, e dove finirebbe. `tabDropTarget` e' un
    // indice nella fila disegnata, `tabDropInto` dice se ci finisce **dentro**
    // (figlio) invece che prima o dopo (fratello).
    int              dragTab       = -1;
    int              tabDropTarget = -1;
    bool             tabDropInto   = false;
    bool             tabDropAfter  = false;
    // Il tab premuto, prima che il gesto diventi un trascinamento: come per la
    // lista, un clic non deve spostare niente finche' il mouse non si muove.
    int              tabPress      = -1;
    float            tabPressX     = 0.0f;
    bool             sidebarVisible = true;
    int              hoveredTab = -1;
    int              hoveredChrome = -1;   // 0 menu, 1 new tab, 2 min, 3 max, 4 close
    int              hoveredSection = -1;  // intestazione di sezione della barra laterale
    int              hoveredNav = -1;      // 0 indietro, 1 avanti, 2 su, 3 aggiorna, 4 segnalibro

    std::wstring directory;

    // Selection is kept by file name, not by entry index: sorting, filtering and
    // refreshing all renumber the entry array, and a selection that slides onto a
    // different file when the user sorts by size is worse than no selection.
    std::wstring cursor;                            // the focused entry
    std::wstring selectionAnchor;                   // Shift+click / Shift+arrow origin
    std::unordered_set<std::wstring> selection;     // entry names

    // Rubber band, in client coordinates.
    bool  rectSelecting = false;
    bool  rectAdditive  = false;
    float rectX0 = 0.0f, rectY0 = 0.0f, rectX1 = 0.0f, rectY1 = 0.0f;

    // Trascinamento. La pressione su una riga non e' ancora un trascinamento: lo
    // diventa quando il mouse si muove oltre la soglia di sistema — sotto quella
    // resta un clic, ed e' l'unico modo di distinguere le due intenzioni. Il punto
    // di partenza serve perche' la soglia si misura da li'.
    bool  dragArmed  = false;
    float dragStartX = 0.0f, dragStartY = 0.0f;

    // La destinazione di una caduta in corso: si evidenzia finche' il puntatore ci
    // sta sopra, ed e' l'unico modo di far vedere DOVE finiranno i file prima di
    // lasciarli. |dropTargetIsRow| distingue una riga dal fondo della lista.
    std::wstring dropTarget;
    bool         dropTargetIsRow = false;
    // Se la destinazione e' una voce del riquadro di navigazione, qual e'. Il
    // riquadro non si evidenzia confrontando percorsi come fa l'elenco: due voci
    // possono puntare alla stessa cartella, e quella accesa e' **quella sotto il
    // puntatore**.
    int          dropTargetSidebarRow = -1;
    // Se l'esito della caduta l'ha gia' detto chi l'ha eseguita. Chiudere un
    // trascinamento riferisce "Spostati N elementi" e non sa *dove*: senza questo,
    // il suo messaggio copre quello piu' preciso della caduta — "Buttati via N
    // elementi nel cestino" diventava "Spostati N elementi".
    bool         dropReported = false;
    // Un trascinamento che parte da una cartella di spazio dei nomi: le voci non
    // hanno un percorso, quindi non si possono "mettere dentro" un'altra cartella
    // nostra. Fuori si trascinano benissimo, dentro no — e il puntatore lo dice.
    bool         draggingNamespaceItems = false;
    // Il percorso del cestino, che non e' una cartella come le altre: tenerlo qui
    // evita di richiederlo alla shell a ogni movimento del mouse durante una caduta.
    std::wstring recycleBinPath;
    // Il nome di spazio dei nomi di "Questo PC". Serve a due cose: salire da una
    // radice di volume ci porta, e la sua etichetta e' "This PC" — il nome del
    // riferimento — invece di quello localizzato che risponde la shell.
    std::wstring thisPcPath;

    // --- colonne (larghezza e filtri per valore) -----------------------------
    //
    // La larghezza e' viva e sta qui: `kColumns` da' quella di partenza. Il filtro
    // per colonna e' un **elenco di valori tolti**, non di valori tenuti: una
    // colonna appena aperta non filtra niente, e questo si legge dalla sua lista
    // vuota senza doverla confrontare con quella dei valori possibili.
    struct ColumnState
    {
        float                 fraction = 0.0f;
        std::set<std::string> excluded;
    };
    std::array<ColumnState, kColumnCount> columns{};

    int          columnDrag = -1;          // bordo trascinato in questo momento
    float        columnDragStartX = 0.0f;
    float        columnDragStartFraction = 0.0f;

    // Il pannello dei valori: aperto su una colonna, con i valori che aveva
    // quando e' stato aperto — si ricalcolano all'apertura, non a ogni fotogramma,
    // perche' contarli vuol dire scorrere tutte le voci.
    int                                     columnPanel = -1;
    std::vector<std::pair<std::string, int>> columnPanelValues;
    GroupMenu                               columnMenu;

    // --- verbi della shell, tenuti per selezione -----------------------------
    //
    // Chiedere alla shell il menu contestuale costa ~65 ms di regime e una ventina
    // di **handle di kernel che non tornano piu' giu'** (misurato, `docs/02`
    // §7.38): le istanze delle estensioni restano vive nel processo. Chiederlo due
    // volte per la stessa selezione e' lavoro ripetuto *e* una perdita, quindi si
    // tiene l'ultimo risultato finche' la selezione non cambia.
    //
    // Gli oggetti della shell stanno qui e non nel menu: il menu li prende in
    // prestito e li restituisce chiudendosi. Chi li rilascia davvero e' questa
    // memoria, quando il risultato non serve piu'.
    struct ShellMenuCache
    {
        bool                          valid      = false;
        bool                          background = false;
        std::wstring                  key;
        std::vector<ContextMenuItem>  items;
        IContextMenu*                 shell  = nullptr;
        HMENU                         handle = nullptr;
        double                        stamp  = 0.0;
    };
    ShellMenuCache shellMenuCache;

    // La geometria della griglia: i rettangoli di ogni riga, senza lo scorrimento.
    // Si rifa' solo quando cambia qualcosa da cui dipende.
    std::vector<Rect> gridRects;
    size_t            gridRectsRows  = static_cast<size_t>(-1);
    float             gridRectsWidth = -1.0f;
    float             gridRectsScale = -1.0f;
    int               gridRectsView  = -1;
    int               gridRectsGroup = -1;
    float             gridRectsIcon  = -1.0f;

    // --- operazioni su file (M7) -------------------------------------------
    //
    // Rinomina sul posto: il campo si disegna sopra la riga, quindi la lista non
    // cambia impaginazione mentre si scrive.
    bool         renameActive = false;
    std::wstring renamePath;        // percorso completo di cio' che si rinomina
    std::wstring renameOriginal;    // nome di partenza, per annullare con Esc
    std::string  renameText;        // il nome in scrittura

    // Vero se l'ultima scrittura in clipboard era un taglio: decide se incollare
    // copia o sposta, ed e' lo stesso bit che Esplora risorse legge.
    bool clipboardCut = false;

    // Messaggio d'esito nella barra di stato. Un'operazione che fallisce in
    // silenzio e' peggio di una che non parte: senza questo, "Sposta" su un file
    // in uso sembrerebbe non aver fatto nulla.
    std::wstring statusMessage;
    double       statusMessageTime = 0.0;

    int   hovered  = -1;
    // Quando il puntatore si e' fermato sulla riga, e dove si trova. Servono a due
    // chiavi dello spec: `HoverToSelect` seleziona dopo una pausa, `ShowTooltips`
    // aspetta la stessa pausa prima di mostrare qualcosa. Una selezione o un
    // suggerimento a ogni movimento del mouse renderebbero l'elenco illeggibile.
    double hoverStart      = 0.0;
    float  pointerX        = 0.0f;
    float  pointerY        = 0.0f;

    // --- interazione (M5d) ---------------------------------------------------
    //
    // Tre chiavi dello spec. Nell'originale sono **impostazioni**, quindi non sono
    // comportamento predefinito: qui si accendono dalla palette. I valori di
    // partenza dell'originale non sono ricostruiti, e questi si dichiarano —
    // i suggerimenti accesi, gli altri due spenti.
    bool hoverToSelect    = false;
    bool underlineOnHover = false;
    bool showTooltips     = true;

    // --- comportamento del clic (M5d) ----------------------------------------
    //
    // Cinque chiavi dello spec, tutte **impostazioni** nell'originale: qui si
    // accendono dalla palette. Tutte spente di partenza — il doppio clic e' il
    // gesto che si aspetta chiunque, e cambiarlo senza che nessuno l'abbia chiesto
    // vorrebbe dire cambiare il programma sotto le mani di chi lo usa.
    //
    // `SingleClickFoldersOnly` vale solo col primo acceso: senza, non vuol dire
    // niente, e infatti da solo non fa niente.
    bool singleClick            = false;   // SingleClick
    bool singleClickFoldersOnly = false;   // SingleClickFoldersOnly
    bool actOnPress             = false;   // ActOnPress
    bool doubleClickToParent    = false;   // DoubleClickToParent
    bool showCheckboxes         = false;   // ShowCheckboxes

    // La misura delle celle nella griglia, in pixel **non** scalati: una sola, da
    // cui discendono icona, cella ed etichetta.
    //
    // `Icons` e `Tiles` non sono due viste diverse: sono **due punti** della stessa
    // misura — la piccola e la grande — ed e' la rotella con Ctrl a portarla dove
    // si vuole, come in Esplora risorse. Prima erano due numeri fissi in una
    // `switch`, e la cella era larga 104/64 di qua e 176/128 di la': due rapporti
    // diversi per la stessa cosa.
    float gridIconSize = 64.0f;
    static constexpr float kGridIconMin = 32.0f;
    static constexpr float kGridIconMax = 256.0f;
    // Sotto questa misura la vista si chiama `Icons`, sopra `Tiles`. Serve al pie'
    // di pagina e alla palette, che devono dire cosa si sta guardando adesso e non
    // cosa si e' premuto l'ultima volta.
    static constexpr float kGridIconSplit = 96.0f;

    // --- M9: le preferenze che il pannello delle opzioni comanda ---------------
    //
    // Sono valori veri, non decorazione: ognuno di questi lo legge un pezzo di
    // vista o di disegno. Un controllo che dichiara qualcosa che la vista non sta
    // facendo e' peggio di un controllo assente.
    std::wstring fontFace    = L"Segoe UI";   // Font
    int          uiFontSize  = 15;             // Font Size (il corpo secondario sta due punti sotto)
    std::wstring monoFace    = L"Consolas";   // Inspector Font
    int          monoFontSize = 14;            // Inspector Font Size
    float        rowSpacing  = 24.0f;          // Spacing: l'altezza di una riga di lista
    bool         alternatingRows = true;       // Alternating Row Colors
    static constexpr float kRowSpacingMin = 18.0f;
    static constexpr float kRowSpacingMax = 40.0f;
    static constexpr int   kUiFontSizeMin = 9;
    static constexpr int   kUiFontSizeMax = 24;

    // Di quanto il corpo secondario e' piu' piccolo di quello principale. **Derivato**
    // e non scelto a parte: chi cambia la dimensione del carattere dalle opzioni
    // vuole che anche le intestazioni, i valori dell'ispettore e la barra di stato
    // seguano, e due numeri indipendenti si scollerebbero al primo ritocco. Il
    // secondario scrive i **dettagli** — il tipo, la dimensione, le date, le
    // intestazioni delle colonne — e sta piu' piccolo perche' si legge **dopo** il
    // nome, non invece del nome.
    static constexpr int   kSmallFontDelta = 4;
    static constexpr int   kMonoFontSizeMin = 8;
    static constexpr int   kMonoFontSizeMax = 24;

    // La misura delle anteprime, in percentuale: 0 le spegne, 100 e' la misura
    // piena, 50 le disegna a meta' — **dove c'e' spazio**, cioe' nella slot
    // dell'icona di una cella o di una riga. Oltre il 100 non si sale: la slot e'
    // quella, e una miniatura piu' grande finirebbe sopra l'etichetta. E' il
    // controllo che il riferimento mette in fondo alla finestra.
    static constexpr float kPreviewSteps[3] = {0.0f, 0.5f, 1.0f};
    static constexpr int   kPreviewStepCount = 3;
    int previewStep = 2;   // 100%
    float PreviewZoom() const { return kPreviewSteps[previewStep]; }

    // Il campo di testo di un controllo `Text`, mentre si scrive: il carattere
    // digitato va qui e non nel filtro.
    bool        optionEditActive = false;
    int         optionEditId     = -1;
    std::string optionEditText;
    // Il cursore di uno slider mentre lo si trascina: -1 quando non si trascina.
    int         optionSliderDrag = -1;

    float scroll   = 0.0f;
    float zoom     = 1.0f;
    float dpiScale = 1.0f;
    int   hoveredPlace = -1;

    SortColumn sortColumn    = SortColumn::Name;
    bool       sortAscending = true;
    ViewMode   viewMode      = ViewMode::Details;

    // View: filter and grouping sit between the directory and the rows.
    std::wstring filter;          // free text; a term may be an extension pattern
    bool         filterActive = false;
    std::vector<Row>   rows;
    std::vector<Group> groups;

    // Two menus, one component.
    GroupMenu palette;                       // Ctrl+Shift+P, item id = CommandId
    GroupMenu goTo;                          // Ctrl+P / F4, item id = index in goToTargets
    // Il pannello delle opzioni (Ctrl+,) e il sottoelenco che un controllo
    // `Choice` apre: due `GroupMenu` distinti perche' il secondo si chiude da solo
    // lasciando aperto il primo.
    GroupMenu options;                       // Ctrl+, item id = OptionId
    GroupMenu optionsChoice;                 // valori di un controllo Choice
    int       optionsChoiceParent = -1;      // OptionId dell'opzione da cui e' nato

    // L'editor della mappa dei tasti: si apre da una riga delle opzioni e ne prende
    // il posto. Gli id delle righe sono `CommandId`, piu' `kKeyMapResetId` in cima.
    // `keyCaptureCommand` e' il comando che sta aspettando il tasto nuovo, -1 quando
    // non si sta aspettando niente: e' l'unico stato della cattura.
    GroupMenu keyMap;
    int       keyCaptureCommand = -1;

    // Le destinazioni di `Fissa in...` e di `Togli da...`: le sezioni della barra
    // laterale che accettano una riga. `pinMenuRemoving` dice **quale delle due
    // domande** sta rispondendo il pannello — la lista e' la stessa, vista al
    // contrario, e per questo e' un menu solo.
    GroupMenu pinMenu;
    bool      pinMenuRemoving = false;
    ContextMenu contextMenu;                 // tasto destro

    // Il pannello delle **proprieta' in-app**: le stesse righe dell'inspector, ma
    // in una finestra nostra, per la sola voce scelta. `Proprieta` del menu lo
    // apre; `Proprieta di Windows` apre ancora quello di sistema, per le cose che
    // solo lui sa (permessi, versioni precedenti, pagine per tipo di file).
    bool                       propertiesOpen = false;
    float                      propertiesScroll = 0.0f;
    std::wstring               propertiesTitle;
    std::wstring               propertiesSubtitle;
    std::vector<InspectorRow>  propertiesRows;

    // --- l'operazione su file in corso ---------------------------------------
    //
    // Copia, spostamento e cestino girano su un thread di lavoro (`FileOps::Job`),
    // perche' una copia da 5 GB non puo' tenere ferma la finestra. Qui c'e' il
    // lavoro, da dove e' partito, e quando e' cominciato — l'ora serve a non far
    // lampeggiare il pannello per un'operazione che finisce in un fotogramma.
    std::unique_ptr<FileOps::Job> operation;
    OperationSource               operationSource = OperationSource::None;
    double                        operationStart  = 0.0;

    // La mappa dei tasti: quale tasto fa quale comando. E' **stato della sessione** —
    // `docs/04` dice cosi' per M9 — quindi vive qui e non in un file: si rimappa, si
    // usa, e uscendo si torna ai tasti di partenza.
    KeyMap keys;
    std::vector<std::wstring> goToTargets;
    std::vector<std::wstring> recents;       // visited this run: StoreRecents = Session
    std::vector<std::wstring> bookmarks;     // this run; persistence is milestone 9
    std::unordered_set<std::string> collapsedGroups;
    GroupMode    groupMode  = GroupMode::None;
    int          matchCount = 0;   // entries surviving the filter, folding aside
    bool         viewDirty  = true;

    // Navigation history of the active tab, saved into and out of it like the
    // rest of the tab state.
    std::vector<std::wstring> history;
    int                       historyIndex = -1;
    uint64_t     viewStatsRevision = 0;
    double       lastViewRebuild   = 0.0;

    std::string errorText;

    double lastFrameTime = 0.0;
    float  framesPerSecond = 0.0f;
    int    viewportWidth = 0;
    int    viewportHeight = 0;
};

// La geometria della finestra, calcolata una volta per fotogramma e passata a chi
// disegna: i rettangoli delle regioni e la scala. E' l'unico tipo che ogni modulo di
// vista riceve, ed e' per questo che sta qui e non accanto a chi la calcola.
struct Layout
{
    Rect titleBar;
    Rect sidebar;
    Rect toolbar;
    Rect header;
    Rect list;
    Rect inspector;
    Rect statusBar;
    Rect filterBar;
    float rowHeight = 0.0f;
    float scale = 1.0f;
};

} // namespace Bolt
