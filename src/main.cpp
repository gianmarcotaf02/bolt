// ---------------------------------------------------------------------------
// Bolt — application shell.
//
// Clone didattico di File Pilot, ricostruito da analisi statica del binario.
//
// Milestone 1 proved the rendering architecture. Milestone 2 adds what makes it
// a file manager rather than a mock-up:
//
//   * real shell icons, keyed by extension (shell_icons.h)
//   * a clickable breadcrumb path bar
//   * sortable columns with a click on the header
//   * navigation: double click to enter a folder, Backspace to go up, F5 to
//     refresh, arrow keys and Enter
//   * an inspector showing metadata taken from the directory entry
//   * the real known folders in the sidebar
//
// Everything still flows through one quad batcher and one draw call per frame.
// ---------------------------------------------------------------------------
#include "diag.h"
#include "dir_stats.h"
#include "drag_drop.h"
#include "file_ops.h"
#include "font_atlas.h"
#include "image_preview.h"
#include "gl_loader.h"
#include "resource.h"
#include "shell_icons.h"
#include "shell_location.h"
#include "shell_new.h"
#include "shell_preview.h"
#include "text_view.h"
#include "shell_sendto.h"
#include "ui_renderer.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
// CMINVOKECOMMANDINFOEX sta in shobjidl.h, non in shlobj.h: serve per eseguire
// una voce del menu della shell.
#include <shobjidl.h>
// Dopo windows.h, come vuole la SDK: le PROPERTYKEY si appoggiano ai tipi di
// sistema. Serve per le proprieta' che il file system non dice.
#include <propkey.h>
#include <propvarutil.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <cwctype>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef REA_SHADER_DIR
#define REA_SHADER_DIR "shaders"
#endif

namespace {

// --- WGL context creation attributes (not in the Windows SDK) --------------
constexpr int kWglContextMajorVersion = 0x2091;
constexpr int kWglContextMinorVersion = 0x2092;
constexpr int kWglContextProfileMask  = 0x9126;
constexpr int kWglContextCoreProfile  = 0x00000001;

using WglCreateContextAttribsArbFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

// --- texture array layers ---------------------------------------------------
// Every UI texture shares one array, so the layer index is the only thing that
// tells a glyph from an icon from a thumbnail.
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
struct Theme
{
    Color windowBg    = Color::FromHex(0x1B1C1F);
    Color panelBg     = Color::FromHex(0x232428);
    Color panelBorder = Color::FromHex(0x32343A);
    Color headerBg    = Color::FromHex(0x1F2024);
    Color rowOdd      = Color::FromHex(0x26272C);
    Color rowSelected = Color::FromHex(0x2F4C7E);
    Color rowHover    = Color::FromHex(0x2C2E34);
    Color text        = Color::FromHex(0xE6E7EA);
    Color textDim     = Color::FromHex(0x9195A0);
    Color accent      = Color::FromHex(0x4C8DFF);
    Color scrollThumb = Color::FromHex(0x4A4D55);
    Color separator   = Color::FromHex(0x2A2C31);
};

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
};

struct Place
{
    std::string  label;
    std::wstring path;

    // Per i volumi: quanto e' pieno, e quanto resta in lettere. Vuoti per tutto
    // il resto — una cartella non ha un'occupazione da mostrare.
    float       usedFraction = 0.0f;
    std::string freeText;
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
    };
    Rect         rect;
    Kind         kind = Kind::Row;
    int          index = 0;      // column index or row index
    std::wstring path;           // for breadcrumbs and places
    std::string  label;          // for group headers
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

const char* ViewModeName(ViewMode mode)
{
    switch (mode)
    {
        case ViewMode::List:  return "List";
        case ViewMode::Tiles: return "Tiles";
        case ViewMode::Icons: return "Icons";
        default:              return "Details";
    }
}

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
// La lingua si sceglie dalla palette e vale per la **sessione**: `docs/06` vieta di
// scrivere un file di impostazioni di nascosto, e prevede che la persistenza, se
// servira' davvero, sia un comando esplicito che dice cosa salva. Predefinita:
// italiano.
enum class Language { Italian, English };
Language gLanguage = Language::Italian;

// Le due lingue di una stringa. Le coppie stanno **dove si usa la stringa**, non in
// un file di traduzioni: si legge cosa diventa senza doverlo cercare altrove, e
// aggiungere una stringa non e' un'operazione in due posti.
const char* Tr(const char* italian, const char* english)
{
    return gLanguage == Language::English ? english : italian;
}
std::wstring TrW(const wchar_t* italian, const wchar_t* english)
{
    return gLanguage == Language::English ? english : italian;
}

enum CommandId
{
    CmdNewTab, CmdCloseTab, CmdNextTab, CmdPrevTab,
    CmdGoUp, CmdBack, CmdForward, CmdRefresh, CmdOpenExplorer, CmdOpenConsole, CmdRecalcStats,
    CmdGoTo,
    CmdToggleSidebar, CmdZoomIn, CmdZoomOut, CmdZoomReset, CmdToggleDetails,
    CmdTextWhitespace, CmdTextLineNumbers, CmdTextMinimap, CmdTextWrap, CmdTextZoomIn,
    CmdTextZoomOut, CmdImageZoomIn, CmdImageZoomOut, CmdImageZoomFit,
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
    CmdToggleBookmark, CmdClearBookmarks, CmdClearRecents,
    // Operazioni su file (M7).
    CmdCopy, CmdCut, CmdPaste, CmdDelete, CmdDeletePermanent, CmdRename, CmdNewFolder,
    // M9: il pannello delle opzioni, raggiungibile anche dalla palette.
    CmdOpenOptions,
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
constexpr int kContextProperties = -2;

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
    const char* label;
    SortColumn  sort;
    float       fraction;   // of the list width, measured from its left edge
    bool        filterable; // se questa colonna offre il filtro per valori
};

// Name, then the columns a directory entry already knows, then the three that
// depend on the recursive statistics and arrive progressively.
const ColumnDef kColumns[] = {
    {"Name",     SortColumn::Name,     0.00f, false},
    {"Type",     SortColumn::Type,     0.38f, true},
    {"Modified", SortColumn::Modified, 0.56f, true},
    // `Folders` e `Files` avevano il titolo appena piu' largo della colonna, e
    // l'imbuto del filtro si mangiava la coda: con il carattere di partenza si
    // sovrapponevano di pochi pixel, con un carattere piu' largo si leggeva
    // `ModifedFoldersFilesSize`. Qui le due colonne hanno lo spazio per il titolo
    // **e** per l'imbuto; se la finestra si stringe, il titolo si accorcia (vedi
    // `DrawHeader`) invece di finire sopra quello vicino.
    {"Folders",  SortColumn::Folders,  0.70f, true},
    {"Files",    SortColumn::Files,    0.805f, true},
    {"Size",     SortColumn::Size,     0.885f, true},
};
constexpr int kColumnCount = static_cast<int>(sizeof(kColumns) / sizeof(kColumns[0]));

// Quanto e' larga una colonna quando la si trascina: sotto questa misura le
// intestazioni non si leggono, e trascinare oltre vorrebbe dire cancellare una
// colonna invece di stringerla.
constexpr float kMinColumnWidth = 44.0f;

struct AppState
{
    // Il contesto di disegno della finestra, per poter disegnare un fotogramma
    // anche fuori dal ciclo principale (vedi `RenderFrame`).
    HDC deviceContext = nullptr;

    UIRenderer      renderer;
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
    Theme             theme;

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
    std::vector<Place> quickAccess;
    std::vector<Place> volumes;
    std::unordered_set<std::string> collapsedSections;

    // Dove punta un collegamento, per percorso. Si risolve una volta per file: la
    // risoluzione apre il collegamento, e una riga si disegna sessanta volte al
    // secondo.
    std::unordered_map<std::wstring, std::wstring> shortcutTargets;

    // Le proprieta' che il file system non dice (le dimensioni di un'immagine, per
    // esempio): anche queste si leggono una volta per percorso.
    std::unordered_map<std::wstring, FileProperties> fileProperties;

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
    static constexpr int   kMonoFontSizeMin = 8;
    static constexpr int   kMonoFontSizeMax = 24;

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
    ContextMenu contextMenu;                 // tasto destro
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

// --- impostazioni ------------------------------------------------------------
//
// Le preferenze si **ricordano**. `docs/06` chiedeva che la persistenza fosse
// esplicita e scelta dall'utente, e l'autore l'ha chiesta cosi': «la lingua deve
// impostarsi in maniera fissa». Quindi il file c'e' — e non e' nascosto:
//
//   %APPDATA%\Bolt\settings.ini
//
// e' testo semplice con una riga per preferenza, si legge e si cancella a mano,
// cancellarlo riporta tutto ai valori di partenza, e l'applicazione **dice dove
// l'ha scritto**. Non contiene nomi di file, cartelle visitate o altre tracce di
// cosa si e' guardato: solo come si vuole che il programma si comporti.
// Il primo tratto del file di impostazioni si legge da una funzione che sta piu'
// in basso: la conversione UTF-8 <-> UTF-16 serve prima della sua definizione.
std::string  ToUtf8(const std::wstring& text);
std::wstring Widen(const std::string& text);

std::wstring SettingsPath()
{
    PWSTR roaming = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) &&
        roaming != nullptr)
    {
        path = roaming;
        CoTaskMemFree(roaming);
    }
    if (path.empty())
        return path;
    path += L"\\Bolt";
    CreateDirectoryW(path.c_str(), nullptr);
    return path + L"\\settings.ini";
}

// Una riga per preferenza, "chiave=valore". Niente librerie e niente formato
// binario: si apre col Blocco note e si capisce.
bool ReadSetting(const std::string& text, const char* key, std::string* value)
{
    const std::string wanted = std::string(key) + "=";
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, wanted.size(), wanted) == 0)
        {
            *value = line.substr(wanted.size());
            return true;
        }
        at = end + 1;
    }
    return false;
}

void LoadSettings(AppState& app)
{
    const std::wstring path = SettingsPath();
    if (path.empty())
        return;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;   // primo avvio: i valori di partenza sono quelli giusti

    std::string text;
    char buffer[1024];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        text.append(buffer, read);
    CloseHandle(file);

    std::string value;
    if (ReadSetting(text, "language", &value))
        gLanguage = (value == "english") ? Language::English : Language::Italian;

    // I nomi dei caratteri sono testo: si leggono cosi' come si scrivono.
    if (ReadSetting(text, "fontFace", &value) && !value.empty())
        app.fontFace = Widen(value);
    if (ReadSetting(text, "monoFace", &value) && !value.empty())
        app.monoFace = Widen(value);

    const auto readFlag = [&](const char* key, bool* target) {
        std::string v;
        if (ReadSetting(text, key, &v))
            *target = (v == "1");
    };
    readFlag("showTooltips", &app.showTooltips);
    readFlag("hoverToSelect", &app.hoverToSelect);
    readFlag("underlineOnHover", &app.underlineOnHover);
    readFlag("singleClick", &app.singleClick);
    readFlag("singleClickFoldersOnly", &app.singleClickFoldersOnly);
    readFlag("actOnPress", &app.actOnPress);
    readFlag("doubleClickToParent", &app.doubleClickToParent);
    readFlag("showCheckboxes", &app.showCheckboxes);
    readFlag("sidebarVisible", &app.sidebarVisible);
    readFlag("alternatingRows", &app.alternatingRows);

    std::string grid;
    if (ReadSetting(text, "gridIconSize", &grid))
        app.gridIconSize = static_cast<float>(atof(grid.c_str()));
    if (app.gridIconSize < AppState::kGridIconMin || app.gridIconSize > AppState::kGridIconMax)
        app.gridIconSize = 64.0f;

    // Le misure si validano: un file modificato a mano non deve poter chiedere un
    // carattere da zero punti o una riga alta un pixel.
    const auto readInt = [&](const char* key, int* target, int low, int high) {
        std::string v;
        if (ReadSetting(text, key, &v))
        {
            const int parsed = atoi(v.c_str());
            *target = (parsed < low) ? low : (parsed > high ? high : parsed);
        }
    };
    readInt("uiFontSize", &app.uiFontSize, AppState::kUiFontSizeMin, AppState::kUiFontSizeMax);
    readInt("monoFontSize", &app.monoFontSize, AppState::kMonoFontSizeMin, AppState::kMonoFontSizeMax);

    std::string spacing;
    if (ReadSetting(text, "rowSpacing", &spacing))
    {
        app.rowSpacing = static_cast<float>(atof(spacing.c_str()));
        if (app.rowSpacing < AppState::kRowSpacingMin)
            app.rowSpacing = AppState::kRowSpacingMin;
        if (app.rowSpacing > AppState::kRowSpacingMax)
            app.rowSpacing = AppState::kRowSpacingMax;
    }
}

// Si scrive a ogni cambio di preferenza: il file e' minuscolo, e una preferenza che
// si perde perche' il programma e' stato chiuso male e' peggio di una scrittura in
// piu'. L'esito lo dice l'applicazione, perche' un salvataggio che fallisce in
// silenzio lascerebbe credere che la scelta sia ricordata.
bool SaveSettings(const AppState& app)
{
    const std::wstring path = SettingsPath();
    if (path.empty())
        return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    std::string text =
        "# Bolt — impostazioni\n"
        "# Le scrive Bolt quando cambi una preferenza, e le rilegge a ogni avvio.\n"
        "# Si cancellano a mano: cancellandole, tutto torna come al primo avvio.\n"
        "# Non contengono nomi di file ne' cartelle visitate.\n";
    text += std::string("language=") +
            (gLanguage == Language::English ? "english" : "italian") + "\n";
    text += "fontFace=" + ToUtf8(app.fontFace) + "\n";
    text += "monoFace=" + ToUtf8(app.monoFace) + "\n";

    const auto writeFlag = [&](const char* key, bool value) {
        text += std::string(key) + "=" + (value ? "1" : "0") + "\n";
    };
    writeFlag("showTooltips", app.showTooltips);
    writeFlag("hoverToSelect", app.hoverToSelect);
    writeFlag("underlineOnHover", app.underlineOnHover);
    writeFlag("singleClick", app.singleClick);
    writeFlag("singleClickFoldersOnly", app.singleClickFoldersOnly);
    writeFlag("actOnPress", app.actOnPress);
    writeFlag("doubleClickToParent", app.doubleClickToParent);
    writeFlag("showCheckboxes", app.showCheckboxes);
    writeFlag("sidebarVisible", app.sidebarVisible);
    writeFlag("alternatingRows", app.alternatingRows);
    text += "gridIconSize=" + std::to_string(static_cast<int>(app.gridIconSize)) + "\n";
    text += "uiFontSize=" + std::to_string(app.uiFontSize) + "\n";
    text += "monoFontSize=" + std::to_string(app.monoFontSize) + "\n";
    text += "rowSpacing=" + std::to_string(static_cast<int>(app.rowSpacing)) + "\n";

    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == text.size();
}

// Ricorda la preferenza e, se non si riesce, lo dice: e' l'unico modo di sapere che
// una scelta non verra' ritrovata al prossimo avvio.
void ReportStatus(AppState& app, const std::wstring& message);

void RememberSettings(AppState& app)
{
    if (!SaveSettings(app))
        ReportStatus(app, TrW(L"Impostazioni non salvate: il file non si e' potuto scrivere",
                              L"Settings not saved: the file could not be written"));
}


// Helpers defined further down but needed earlier: the selection queries by the
// drawing code, and the parent path by the command palette.
int  FindEntryByName(const AppState& app, const std::wstring& name);
int  CursorIndex(const AppState& app);
bool IsEntrySelected(const AppState& app, const Entry& entry);
std::wstring ParentOf(const std::wstring& path);
double       NowSeconds();
std::wstring Widen(const std::string& text);

// --- string helpers --------------------------------------------------------
std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

// Directory and name joined with exactly one separator, so a drive root such as
// "C:\\" does not become "C:\\\\name".
std::wstring JoinPath(const std::wstring& directory, const std::wstring& name)
{
    if (directory.empty())
        return name;
    if (directory.back() == L'\\')
        return directory + name;
    return directory + L"\\" + name;
}

std::string HumanBytes(uint64_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), (unit == 0) ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buffer;
}

// Compact count formatting for the Folders and Files columns, in the style the
// reference application uses: 89, 2.4K, 18.4K, 131K, 109.3K.
std::string HumanCount(uint64_t value)
{
    if (value < 1000)
        return std::to_string(value);

    static const char* suffixes[] = {"K", "M", "B"};
    double scaled = static_cast<double>(value) / 1000.0;
    int suffix = 0;
    while (scaled >= 1000.0 && suffix < 2)
    {
        scaled /= 1000.0;
        ++suffix;
    }

    // Round to one decimal, then drop the decimal when it carries nothing: a
    // count of 131 000 reads better as "131K" than as "131.0K".
    const double tenths = std::floor(scaled * 10.0 + 0.5) / 10.0;
    char buffer[32];
    if (tenths == std::floor(tenths))
        std::snprintf(buffer, sizeof(buffer), "%.0f%s", tenths, suffixes[suffix]);
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f%s", tenths, suffixes[suffix]);
    return buffer;
}

std::string FormatFileTime(const FILETIME& fileTime, bool withTime)
{
    FILETIME local{};
    SYSTEMTIME system{};
    if (FileTimeToLocalFileTime(&fileTime, &local) == FALSE ||
        FileTimeToSystemTime(&local, &system) == FALSE)
        return "-";

    char buffer[64];
    if (withTime)
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u",
                      system.wYear, system.wMonth, system.wDay, system.wHour, system.wMinute);
    else
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u",
                      system.wYear, system.wMonth, system.wDay);
    return buffer;
}

uint64_t FileTimeToUint64(const FILETIME& fileTime)
{
    return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
}

// Truncates |text| with a trailing ellipsis so it cannot spill into the next
// column. The renderer has no scissor rectangle, so fitting has to happen here.
const FileProperties& PropertiesOf(AppState& app, const std::wstring& path)
{
    const auto cached = app.fileProperties.find(path);
    if (cached != app.fileProperties.end())
        return cached->second;

    FileProperties properties;
    IPropertyStore* store = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT,
                                                    IID_IPropertyStore,
                                                    reinterpret_cast<void**>(&store))) &&
        store != nullptr)
    {
        PROPVARIANT value;
        ULONG number = 0;

        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Image_HorizontalSize, &value)) &&
            SUCCEEDED(PropVariantToUInt32(value, &number)))
            properties.imageWidth = number;
        PropVariantClear(&value);

        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Image_VerticalSize, &value)) &&
            SUCCEEDED(PropVariantToUInt32(value, &number)))
            properties.imageHeight = number;
        PropVariantClear(&value);

        store->Release();
    }

    return app.fileProperties.emplace(path, properties).first->second;
}

// Un volume: il file system e lo spazio. Non sono proprieta' del **file** ma del
// volume che lo contiene, e il posto giusto per chiederle e' il sistema — lo dice
// anche `docs/03`: l'occupazione dei volumi si ottiene con `GetDiskFreeSpaceExW`
// per ogni volume.
struct VolumeInfo
{
    bool        isVolume = false;
    std::string fileSystem;
    std::string freeText;
    std::string totalText;
    float       usedFraction = 0.0f;
};

VolumeInfo VolumeOf(const std::wstring& location)
{
    VolumeInfo info;
    if (location.empty())
        return info;

    // La domanda e' "questo percorso **e'** la radice di un volume", non "sta su un
    // volume". `GetDriveTypeW` risponde `DRIVE_FIXED` anche per una cartella in
    // mezzo al disco — risolve il percorso al volume che lo contiene — quindi ogni
    // file e ogni cartella su `C:` diventava un volume, con la capacita' del disco
    // al posto della propria dimensione: 952,6 GB per una cartella da 36 byte.
    //
    // Il modo giusto e' chiedere **qual e'** la radice del volume e confrontarla col
    // percorso. Regge anche i volumi montati dentro una cartella, che una radice di
    // unita' non li copre.
    wchar_t root[MAX_PATH]{};
    if (GetVolumePathNameW(location.c_str(), root, MAX_PATH) == FALSE)
        return info;

    const auto trim = [](std::wstring text) {
        while (text.size() > 1 && text.back() == L'\\')
            text.pop_back();
        return text;
    };
    if (_wcsicmp(trim(location).c_str(), trim(root).c_str()) != 0)
        return info;   // e' una cartella o un file che sta **sopra** un volume

    info.isVolume = true;

    wchar_t fileSystem[64]{};
    if (GetVolumeInformationW(location.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fileSystem,
                              64) != FALSE)
        info.fileSystem = ToUtf8(fileSystem);

    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    if (GetDiskFreeSpaceExW(location.c_str(), &available, &total, nullptr) && total.QuadPart > 0)
    {
        info.freeText  = HumanBytes(available.QuadPart);
        info.totalText = HumanBytes(total.QuadPart);
        info.usedFraction = 1.0f - static_cast<float>(static_cast<double>(available.QuadPart) /
                                                     static_cast<double>(total.QuadPart));
    }
    return info;
}

// Dove punta un collegamento. Un ".lnk" da solo non dice cosa apre, e il bersaglio
// accanto al nome e' quello che si vuole sapere prima di aprirlo: e' quello che fa
// File Pilot, ed e' l'unica informazione del collegamento che serva davvero.
//
// La lettura apre il file, quindi si fa una volta per percorso e si tiene: la riga
// si disegna a ogni fotogramma.
const std::wstring& ShortcutTarget(AppState& app, const Entry& entry)
{
    static const std::wstring empty;

    if (entry.extension != "lnk" || !entry.location.empty())
        return empty;   // fuori da una cartella normale un collegamento non si risolve

    // Il percorso: qui la voce e' per forza di una cartella normale, quindi il nome
    // dentro la cartella aperta e' tutto quello che serve.
    const std::wstring path = JoinPath(app.directory, entry.wideName);
    const auto cached = app.shortcutTargets.find(path);
    if (cached != app.shortcutTargets.end())
        return cached->second;

    std::wstring target;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&link))) &&
        link != nullptr)
    {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file))) &&
            file != nullptr)
        {
            if (SUCCEEDED(file->Load(path.c_str(), STGM_READ)))
            {
                wchar_t buffer[MAX_PATH]{};
                if (SUCCEEDED(link->GetPath(buffer, MAX_PATH, nullptr, SLGP_RAWPATH)))
                    target = buffer;

                // Un collegamento non punta sempre a un file: puo' puntare a una
                // cartella dello spazio dei nomi, e allora l'identificatore e' il
                // PIDL, non un percorso.
                if (target.empty())
                {
                    PIDLIST_ABSOLUTE pidl = nullptr;
                    if (SUCCEEDED(link->GetIDList(&pidl)) && pidl != nullptr)
                    {
                        PWSTR name = nullptr;
                        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) &&
                            name != nullptr)
                        {
                            target = name;
                            CoTaskMemFree(name);
                        }
                        CoTaskMemFree(pidl);
                    }
                }
            }
            file->Release();
        }
        link->Release();
    }

    return app.shortcutTargets.emplace(path, std::move(target)).first->second;
}

std::string FitText(const FontAtlas& font, const std::string& text, float scale, float maxWidth)
{
    if (maxWidth <= 0.0f || text.empty())
        return {};
    if (font.Measure(text, scale) <= maxWidth)
        return text;

    const std::string ellipsis = "...";
    const float ellipsisWidth = font.Measure(ellipsis, scale);

    std::string truncated;
    float width = 0.0f;
    for (size_t i = 0; i < text.size(); i = Utf8::Next(text, i))
    {
        // Il taglio cade fra un carattere e l'altro, mai dentro: troncare a meta'
        // una sequenza UTF-8 lascia un byte che il disegno legge come un altro
        // carattere, e in fondo al nome compare un simbolo che non c'e'.
        const size_t length = Utf8::Length(text, i);
        const float advance = font.MeasureOne(Utf8::At(text, i, nullptr), scale);
        if (width + advance + ellipsisWidth > maxWidth)
            break;
        truncated.append(text, i, length);
        width += advance;
    }
    return truncated + ellipsis;
}

// Come FitText, ma taglia **davanti**: per un percorso la parte che identifica e' la
// coda, e "...\sandbox\relazione.txt" si legge meglio di "...\Progetti\Antigravity\...".
std::string FitTextTail(const FontAtlas& font, const std::string& text, float scale, float maxWidth)
{
    if (maxWidth <= 0.0f || text.empty() || font.Measure(text, scale) <= maxWidth)
        return text;

    const std::string ellipsis = "...";
    const float ellipsisWidth = font.Measure(ellipsis, scale);

    // Si parte dal fondo e si cammina all'indietro, un carattere alla volta: cosi'
    // il taglio cade fra due caratteri e non a meta' di una sequenza UTF-8.
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); i = Utf8::Next(text, i))
        starts.push_back(i);

    float width = 0.0f;
    size_t keepFrom = text.size();
    for (size_t i = starts.size(); i > 0; --i)
    {
        const size_t index = starts[i - 1];
        const float advance = font.MeasureOne(Utf8::At(text, index, nullptr), scale);
        if (width + advance + ellipsisWidth > maxWidth)
            break;
        width += advance;
        keepFrom = index;
    }
    return ellipsis + text.substr(keepFrom);
}

// Shell type name for an extension, cached: "PNG image" reads better than "PNG file".
std::string ShellTypeName(const std::string& extension, bool isDirectory)
{
    static std::unordered_map<std::string, std::string> cache;
    const std::string key = isDirectory ? std::string("<dir>") : extension;

    const auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second;

    const std::wstring synthetic = isDirectory ? L"folder" : (L"x." + Widen(extension));
    const DWORD attributes = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;

    SHFILEINFOW info{};
    std::string result;
    if (SHGetFileInfoW(synthetic.c_str(), attributes, &info, sizeof(info),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) != 0)
        result = ToUtf8(info.szTypeName);

    if (result.empty())
        result = isDirectory ? "File folder" : (extension.empty() ? "File" : extension + " file");

    cache.emplace(key, result);
    return result;
}

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

std::vector<Entry> ReadDirectory(const std::wstring& directory, std::string* error)
{
    std::vector<Entry> entries;
    error->clear();

    // Il cestino non si elenca con FindFirstFile, e non e' un percorso: le sue
    // voci le sa solo la shell.
    if (ShellLocation::IsNamespace(directory))
        return ReadNamespaceDirectory(directory, error);

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = directory + L"\\*";
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
        *error = (code == ERROR_ACCESS_DENIED) ? "Access denied." : "Cannot read this folder.";
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

bool SortEntries(AppState& app)
{
    const bool ascending = app.sortAscending;
    const SortColumn column = app.sortColumn;

    // A statistics column needs the service, which costs a lock per comparison.
    // That is acceptable here because sorting happens on demand, not per frame.
    auto statsFor = [&app](const Entry& entry, DirStats* out) -> bool
    {
        if (!entry.isDirectory)
            return false;
        return app.stats.TryGet(JoinPath(app.directory, entry.wideName), out);
    };

    auto statValue = [&statsFor](const Entry& entry, SortColumn which) -> uint64_t
    {
        if (!entry.isDirectory)
            return (which == SortColumn::Size) ? entry.bytes : 0;

        DirStats stats;
        if (!statsFor(entry, &stats))
            return 0;
        switch (which)
        {
            case SortColumn::Folders: return stats.folders;
            case SortColumn::Files:   return stats.files;
            default:                  return stats.logicalBytes;
        }
    };

    std::stable_sort(app.entries.begin(), app.entries.end(),
                     [column, ascending, &statValue](const Entry& left, const Entry& right)
                     {
                         // Folders always lead, matching the FoldersFirst default.
                         if (left.isDirectory != right.isDirectory)
                             return left.isDirectory;

                         int order = 0;
                         switch (column)
                         {
                             case SortColumn::Size:
                             case SortColumn::Folders:
                             case SortColumn::Files:
                             {
                                 const uint64_t a = statValue(left, column);
                                 const uint64_t b = statValue(right, column);
                                 order = (a < b) ? -1 : (a > b) ? 1 : 0;
                                 break;
                             }
                             case SortColumn::Modified:
                             {
                                 const uint64_t a = FileTimeToUint64(left.modified);
                                 const uint64_t b = FileTimeToUint64(right.modified);
                                 order = (a < b) ? -1 : (a > b) ? 1 : 0;
                                 break;
                             }
                             case SortColumn::Type:
                                 order = left.typeText.compare(right.typeText);
                                 break;
                             case SortColumn::Name:
                             default:
                                 order = _wcsicmp(left.wideName.c_str(), right.wideName.c_str());
                                 break;
                         }

                         if (order == 0)
                             order = _wcsicmp(left.wideName.c_str(), right.wideName.c_str());

                         return ascending ? (order < 0) : (order > 0);
                     });
    return true;
}

// --- view: filtering --------------------------------------------------------
std::wstring ToLowerWide(const std::wstring& text)
{
    std::wstring out = text;
    for (wchar_t& c : out)
        c = static_cast<wchar_t>(towlower(c));
    return out;
}

// '*' matches any run, '?' one character. Iterative with backtracking on the
// last star, which is the classic linear-time formulation.
bool GlobMatch(const std::wstring& pattern, const std::wstring& text)
{
    size_t p = 0, t = 0;
    size_t star = std::wstring::npos;
    size_t resume = 0;

    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t]))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == L'*')
        {
            star = p++;
            resume = t;
        }
        else if (star != std::wstring::npos)
        {
            p = star + 1;
            t = ++resume;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == L'*')
        ++p;
    return p == pattern.size();
}

bool TermMatches(const Entry& entry, const std::wstring& term)
{
    // A term that starts with a dot, or contains a wildcard, asks about the
    // type rather than the whole name: ".png" and "*.png" mean the same thing.
    // This is the filter-by-type path of the reference key table
    // (FilterFileTypes / FilterTypes) expressed in the same box as free text.
    if (!term.empty() && term[0] == L'.' && term.find(L'*') == std::wstring::npos)
    {
        if (entry.isDirectory)
            return false;
        return ToLowerWide(Widen(entry.extension)) == term.substr(1);
    }

    const std::wstring name = ToLowerWide(entry.wideName);
    if (term.find(L'*') != std::wstring::npos || term.find(L'?') != std::wstring::npos)
        return GlobMatch(term, name);
    return name.find(term) != std::wstring::npos;
}

// Space separated terms, all of which must match. An empty filter matches all.
bool FilterMatch(const Entry& entry, const std::wstring& filter);

// Il valore di una colonna per una voce, **come si vede**: la stessa stringa che
// disegna la cella. Un filtro che parla una lingua diversa da quella della colonna
// e' un filtro che non si capisce.
//
// La dimensione fa eccezione ed e' dichiarata: un valore per file non filtra
// niente, quindi li' il valore e' una **fascia** ("< 1 KB"), che e' quello che
// chiede chi cerca "i file piccoli".
std::string ColumnValueOf(const AppState& app, const Entry& entry, int column)
{
    switch (column)
    {
        case 1: return entry.typeText;
        case 2: return entry.modifiedText.empty() ? std::string("--") : entry.modifiedText;
        case 3:
        case 4:
        {
            if (!entry.isDirectory || !entry.location.empty())
                return "--";
            DirStats stats;
            if (!app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats))
                return "--";
            return HumanCount(column == 3 ? stats.folders : stats.files);
        }
        case 5:
        {
            uint64_t bytes = entry.bytes;
            if (entry.isDirectory)
            {
                if (entry.location.empty())
                {
                    DirStats stats;
                    if (!app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats))
                        return "--";
                    bytes = stats.logicalBytes;
                }
                else
                {
                    return "--";
                }
            }
            if (bytes == 0)
                return "0 B";
            if (bytes < 1024ull)
                return "< 1 KB";
            if (bytes < 1024ull * 1024ull)
                return "< 1 MB";
            if (bytes < 1024ull * 1024ull * 1024ull)
                return "< 1 GB";
            return ">= 1 GB";
        }
        default: return std::string();
    }
}

// Una colonna senza valori tolti non filtra niente, e le colonne si sommano: chi
// cerca "documenti di testo modificati oggi" toglie valori in due colonne diverse.
bool ColumnFiltersMatch(const AppState& app, const Entry& entry)
{
    for (int i = 0; i < kColumnCount; ++i)
    {
        if (app.columns[static_cast<size_t>(i)].excluded.empty())
            continue;
        const std::string value = ColumnValueOf(app, entry, i);
        if (app.columns[static_cast<size_t>(i)].excluded.count(value) != 0)
            return false;
    }
    return true;
}

bool EntryVisible(const AppState& app, const Entry& entry)
{
    return FilterMatch(entry, app.filter) && ColumnFiltersMatch(app, entry);
}

bool FilterMatch(const Entry& entry, const std::wstring& filter)
{
    if (filter.empty())
        return true;

    size_t start = 0;
    for (;;)
    {
        size_t space = filter.find(L' ', start);
        if (space == std::wstring::npos)
            space = filter.size();
        if (space > start && !TermMatches(entry, filter.substr(start, space - start)))
            return false;
        if (space >= filter.size())
            break;
        start = space + 1;
    }
    return true;
}

// --- view: grouping ---------------------------------------------------------
uint64_t LocalDayIndex(const FILETIME& fileTime)
{
    FILETIME local{};
    if (FileTimeToLocalFileTime(&fileTime, &local) == FALSE)
        return 0;
    const uint64_t ticks = (static_cast<uint64_t>(local.dwHighDateTime) << 32) | local.dwLowDateTime;
    return ticks / (10000000ULL * 60ULL * 60ULL * 24ULL);
}

uint64_t CurrentDayIndex()
{
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return LocalDayIndex(now);
}

bool VolumeUsage(const std::wstring& path, int* percentFull)
{
    if (path.size() < 2 || path[1] != L':')
        return false;
    const wchar_t root[4] = {path[0], L':', L'\\', L'\0'};
    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(root, &available, &total, &freeBytes) == FALSE || total.QuadPart == 0)
        return false;
    *percentFull = static_cast<int>((total.QuadPart - freeBytes.QuadPart) * 100 / total.QuadPart);
    return true;
}

// The band names come from the reference key table (GroupByLabel_*). The
// cut-offs do not: docs/03 records them as still unknown, so the values here are
// a reconstruction and are deliberately easy to find and change.
void GroupKeyFor(const Entry& entry, GroupMode mode, uint64_t bytes, uint64_t items,
                 bool statsKnown, int percentFull, int* outOrder, std::string* outLabel)
{
    switch (mode)
    {
        case GroupMode::Name:
        {
            const wchar_t c = entry.wideName.empty()
                ? L'\0'
                : static_cast<wchar_t>(towupper(entry.wideName[0]));
            if (c >= L'0' && c <= L'9')      { *outOrder = 0; *outLabel = "09"; }
            else if (c >= L'A' && c <= L'H') { *outOrder = 1; *outLabel = "AH"; }
            else if (c >= L'I' && c <= L'P') { *outOrder = 2; *outLabel = "IP"; }
            else if (c >= L'Q' && c <= L'Z') { *outOrder = 3; *outLabel = "QZ"; }
            else                             { *outOrder = 4; *outLabel = "Other"; }
            break;
        }
        case GroupMode::Date:
        {
            const uint64_t now  = CurrentDayIndex();
            const uint64_t then = LocalDayIndex(entry.modified);
            const uint64_t age  = (now >= then) ? (now - then) : 0;
            if (age == 0)        { *outOrder = 0; *outLabel = "Today"; }
            else if (age == 1)   { *outOrder = 1; *outLabel = "Yesterday"; }
            else if (age <= 6)   { *outOrder = 2; *outLabel = "EarlierThisWeek"; }
            else if (age <= 13)  { *outOrder = 3; *outLabel = "LastWeek"; }
            else if (age <= 30)  { *outOrder = 4; *outLabel = "EarlierThisMonth"; }
            else if (age <= 60)  { *outOrder = 5; *outLabel = "LastMonth"; }
            else if (age <= 364) { *outOrder = 6; *outLabel = "EarlierThisYear"; }
            else if (age <= 729) { *outOrder = 7; *outLabel = "LastYear"; }
            else                 { *outOrder = 8; *outLabel = "Older"; }
            break;
        }
        case GroupMode::Size:
        {
            if (entry.isDirectory && !statsKnown) { *outOrder = 7; *outLabel = "Unspecified"; break; }
            if (bytes == 0)                          { *outOrder = 0; *outLabel = "Empty"; }
            else if (bytes < 16ULL * 1024)           { *outOrder = 1; *outLabel = "Tiny"; }
            else if (bytes < 1024ULL * 1024)         { *outOrder = 2; *outLabel = "Small"; }
            else if (bytes < 16ULL * 1024 * 1024)    { *outOrder = 3; *outLabel = "Medium"; }
            else if (bytes < 128ULL * 1024 * 1024)   { *outOrder = 4; *outLabel = "Large"; }
            else if (bytes < 1024ULL * 1024 * 1024)  { *outOrder = 5; *outLabel = "Huge"; }
            else                                     { *outOrder = 6; *outLabel = "Gigantic"; }
            break;
        }
        case GroupMode::Count:
        {
            if (!entry.isDirectory || !statsKnown) { *outOrder = 8; *outLabel = "Unspecified"; break; }
            if (items == 0)          { *outOrder = 0; *outLabel = "Empty"; }
            else if (items <= 5)     { *outOrder = 1; *outLabel = "Few"; }
            else if (items <= 50)    { *outOrder = 2; *outLabel = "Dozens"; }
            else if (items <= 500)   { *outOrder = 3; *outLabel = "Hundreds"; }
            else if (items <= 5000)  { *outOrder = 4; *outLabel = "Thousands"; }
            else if (items <= 50000) { *outOrder = 5; *outLabel = "TensOfThousands"; }
            else if (items <= 500000){ *outOrder = 6; *outLabel = "HundredsOfThousands"; }
            else                     { *outOrder = 7; *outLabel = "Millions"; }
            break;
        }
        case GroupMode::Volume:
        default:
        {
            if (percentFull < 0) { *outOrder = 11; *outLabel = "Unspecified"; break; }
            const int band = (percentFull >= 100) ? 10 : percentFull / 10;
            *outOrder = band;
            if (band >= 10)
            {
                *outLabel = "100%";
            }
            else
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%d-%d%%", band * 10, band * 10 + 9);
                *outLabel = buffer;
            }
            break;
        }
    }
}

const char* GroupModeName(GroupMode mode)
{
    switch (mode)
    {
        case GroupMode::Name:   return "Name";
        case GroupMode::Date:   return "Date";
        case GroupMode::Size:   return "Size";
        case GroupMode::Count:  return "Count";
        case GroupMode::Volume: return "Volume";
        default:                return "None";
    }
}

void RebuildView(AppState& app)
{
    app.rows.clear();
    app.groups.clear();
    app.matchCount = 0;
    app.viewDirty = false;

    // Le bande di gruppo adesso stanno anche nella griglia: la geometria e' un
    // impaginatore a flusso che sa spezzarsi, e la usano tutti — vedi `GridRects`.
    if (app.groupMode == GroupMode::None)
    {
        for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
        {
            if (EntryVisible(app, app.entries[static_cast<size_t>(i)]))
                app.rows.push_back(Row{false, i, -1});
        }
        app.matchCount = static_cast<int>(app.rows.size());
        return;
    }

    int percentFull = -1;
    if (app.groupMode == GroupMode::Volume)
    {
        int percent = 0;
        if (VolumeUsage(app.directory, &percent))
            percentFull = percent;
    }

    struct Bucket
    {
        int              order = 0;
        std::string      label;
        std::vector<int> entries;
    };

    std::vector<Bucket> buckets;
    std::unordered_map<int, int> bucketIndex;

    const auto addToBucket = [&buckets, &bucketIndex](int order, const std::string& label, int entryIndex)
    {
        const auto found = bucketIndex.find(order);
        int index = 0;
        if (found == bucketIndex.end())
        {
            index = static_cast<int>(buckets.size());
            bucketIndex[order] = index;
            buckets.push_back(Bucket{order, label, {}});
        }
        else
        {
            index = found->second;
        }
        buckets[static_cast<size_t>(index)].entries.push_back(entryIndex);
    };

    for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
    {
        const Entry& entry = app.entries[static_cast<size_t>(i)];
        if (!EntryVisible(app, entry))
            continue;

        uint64_t bytes = entry.isDirectory ? 0 : entry.bytes;
        uint64_t items = 0;
        bool statsKnown = true;
        if (entry.isDirectory)
        {
            DirStats stats;
            statsKnown = app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats);
            if (statsKnown)
            {
                bytes = stats.logicalBytes;
                items = stats.files + stats.folders;
            }
        }

        int order = 0;
        std::string label;
        GroupKeyFor(entry, app.groupMode, bytes, items, statsKnown, percentFull, &order, &label);
        addToBucket(order, label, i);
        ++app.matchCount;
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const Bucket& a, const Bucket& b) { return a.order < b.order; });

    for (const Bucket& bucket : buckets)
    {
        app.groups.push_back(Group{bucket.label, static_cast<int>(bucket.entries.size())});
        const int groupIndex = static_cast<int>(app.groups.size()) - 1;
        app.rows.push_back(Row{true, -1, groupIndex});

        if (app.collapsedGroups.count(bucket.label) != 0)
            continue;
        for (const int entryIndex : bucket.entries)
            app.rows.push_back(Row{false, entryIndex, -1});
    }
}

// Last path component, which is what a tab is labelled with. A drive root keeps
// its colon and slash so "C:\" does not become an empty label.
std::wstring LeafName(const std::wstring& path)
{
    if (path.empty())
        return L"This PC";
    std::wstring trimmed = path;
    while (trimmed.size() > 3 && trimmed.back() == L'\\')
        trimmed.pop_back();
    const size_t slash = trimmed.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return trimmed;
    const std::wstring leaf = trimmed.substr(slash + 1);
    return leaf.empty() ? trimmed : leaf;
}

// Il nome assoluto di una voce: quello che si e' letto per le cartelle dello
// spazio dei nomi, o il percorso dentro la cartella aperta per tutto il resto.
//
// Un solo posto decide, cosi' aprire, selezionare e chiedere un verbo alla shell
// non possono divergere sul significato di "dove sta questa voce".
std::wstring EntryLocation(const AppState& app, const Entry& entry)
{
    return entry.location.empty() ? JoinPath(app.directory, entry.wideName) : entry.location;
}

// Come si chiama una posizione: il suo nome se e' un percorso, il nome che le da'
// la shell se e' una cartella dello spazio dei nomi — "Cestino", non
// "::{645FF040-...}".
std::wstring LocationLabel(const std::wstring& location)
{
    if (location.empty())
        return L"This PC";
    if (!ShellLocation::IsNamespace(location))
        return location;
    const std::wstring name = ShellLocation::DisplayName(location);
    return name.empty() ? location : name;
}

void UpdateWindowTitle(AppState& app)
{
    if (app.window == nullptr)
        return;
    const std::wstring title = L"Bolt — " + LocationLabel(app.directory);
    SetWindowTextW(app.window, title.c_str());
}

// |recordHistory| is false only when the move is itself a history step, or
// walking back would push the place you are leaving onto the stack again.
void LoadDirectory(AppState& app, const std::wstring& directory, bool recordHistory = true)
{
    const std::wstring normalised = directory.empty() ? L"" : directory;
    // Il filtro non segue l'utente in un'altra cartella — un filtro rimasto da un
    // altro elenco sembrerebbe una cartella vuota — ma **ricaricare la stessa
    // cartella non e' cambiare cartella**: premere F5 non deve svuotare la casella
    // del filtro, che sta sotto gli occhi ed e' una cosa che l'utente ha scritto.
    const bool sameFolder = (normalised == app.directory);

    app.entries = ReadDirectory(normalised, &app.errorText);
    app.directory = normalised;
    app.previews.CancelPending();
    app.stats.CancelQueued();

    if (!sameFolder)
    {
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

    SortEntries(app);
    if (!app.entries.empty())
    {
        app.cursor = app.entries.front().wideName;
        app.selection.insert(app.cursor);
        app.selectionAnchor = app.cursor;
    }
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

    // StoreRecents has three modes in the reference key table (LocalDisk,
    // Persistent, Session). This is Session: nothing is written to disk yet.
    app.recents.erase(std::remove(app.recents.begin(), app.recents.end(), normalised),
                      app.recents.end());
    app.recents.insert(app.recents.begin(), normalised);
    if (app.recents.size() > 20)
        app.recents.resize(20);

    UpdateWindowTitle(app);
}

// --- tabs -------------------------------------------------------------------
void SaveActiveTab(AppState& app)
{
    if (app.activeTab < 0 || app.activeTab >= static_cast<int>(app.tabs.size()))
        return;

    Tab& tab = app.tabs[static_cast<size_t>(app.activeTab)];
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
    if (app.activeTab < 0 || app.activeTab >= static_cast<int>(app.tabs.size()))
        return;

    Tab& tab = app.tabs[static_cast<size_t>(app.activeTab)];
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

// --- l'albero dei tab -------------------------------------------------------
//
// I tab sono un albero, non una fila: un tab puo' avere figli, e un figlio si
// disegna **sotto** il suo genitore. La fila si ricava dall'albero a ogni
// fotogramma invece di essere l'ordine in cui i tab sono nati, cosi' chiudere o
// spostare non deve aggiustare l'indice di tutti gli altri — che e' il modo in cui
// un albero diventa un groviglio.
int TabById(const AppState& app, int id)
{
    for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i)
    {
        if (app.tabs[static_cast<size_t>(i)].id == id)
            return i;
    }
    return -1;
}

int TabRootId(const AppState& app, int id)
{
    // Il ciclo si ferma anche se qualcuno, un giorno, chiudesse un anello: un
    // albero sbagliato deve disegnarsi male, non bloccare la finestra.
    for (int guard = 0; guard < 64; ++guard)
    {
        const int index = TabById(app, id);
        if (index < 0)
            return id;
        const int parent = app.tabs[static_cast<size_t>(index)].parentId;
        if (parent < 0)
            return id;
        id = parent;
    }
    return id;
}

bool TabIsDescendantOf(const AppState& app, int id, int ancestorId)
{
    if (id == ancestorId)
        return true;
    return TabRootId(app, id) == ancestorId;
}

const ClickTarget* HitTest(const AppState& app, float x, float y, ClickTarget::Kind kind);

// Dove finirebbe il tab trascinato, dato il punto del puntatore. Sta qui e non nel
// gestore del mouse perche' il marcatore disegnato e il rilascio devono leggere la
// stessa risposta: due calcoli che devono coincidere prima o poi non coincidono.
void UpdateTabDrop(AppState& app, float x, float y)
{
    app.tabDropTarget = -1;
    const ClickTarget* hit = HitTest(app, x, y, ClickTarget::Kind::Tab);
    if (hit == nullptr)
        return;
    const int index = hit->index;
    if (app.tabs[static_cast<size_t>(index)].id == app.dragTab)
        return;   // sopra se stesso non si va da nessuna parte

    // Il terzo di mezzo vuol dire "dentro": e' la zona piu' stretta, cosi' i due
    // bordi restano comodi per mettersi prima o dopo.
    const float third = hit->rect.w / 3.0f;
    const float left  = hit->rect.x;
    app.tabDropTarget = index;
    if (x >= left + third && x <= left + third * 2.0f)
    {
        app.tabDropInto = true;
        return;
    }
    app.tabDropInto  = false;
    app.tabDropAfter = (x > left + third);
    DiagLog("  trascinamento di un tab: x " + std::to_string(x) + ", tab " +
            std::to_string(index) + " da " + std::to_string(left) + " a " +
            std::to_string(left + hit->rect.w) + ", terzo " + std::to_string(third) +
            ", esito " + (app.tabDropAfter ? "dopo" : "prima"));
}

// La fila come si vede: ogni coppia e' (indice del tab, quanto e' profondo).
std::vector<std::pair<int, int>> TabOrder(const AppState& app)
{
    std::vector<std::pair<int, int>> out;
    out.reserve(app.tabs.size());

    // I figli si mettono subito dopo il genitore, ricorsivamente: un ramo alla
    // volta, nell'ordine di nascita. Le radici in fondo.
    std::function<void(int, int)> walk = [&](int parentId, int depth)
    {
        for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i)
        {
            if (app.tabs[static_cast<size_t>(i)].parentId != parentId)
                continue;
            out.push_back({i, depth});
            walk(app.tabs[static_cast<size_t>(i)].id, depth + 1);
        }
    };
    walk(-1, 0);
    return out;
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

    app.tabs.push_back(std::move(tab));
    app.activeTab = static_cast<int>(app.tabs.size()) - 1;

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
    if (index < 0 || index >= static_cast<int>(app.tabs.size()) || index == app.activeTab)
        return;

    SaveActiveTab(app);
    app.activeTab = index;
    LoadActiveTab(app);
    app.stats.CancelQueued();
    app.previews.CancelPending();
}

void CloseTab(AppState& app, int index)
{
    if (index < 0 || index >= static_cast<int>(app.tabs.size()))
        return;
    if (app.tabs.size() == 1)
        return;   // the last tab is not closable: use the window button instead

    const bool closingActive = (index == app.activeTab);
    app.tabs.erase(app.tabs.begin() + index);

    if (index < app.activeTab)
    {
        // The active tab merely shifted down; the live state still matches it.
        --app.activeTab;
        return;
    }

    if (closingActive)
    {
        if (app.activeTab >= static_cast<int>(app.tabs.size()))
            app.activeTab = static_cast<int>(app.tabs.size()) - 1;
        LoadActiveTab(app);
        app.stats.CancelQueued();
        app.previews.CancelPending();
    }
}

// Sposta il tab trascinato dove il marcatore dice.
void ApplyTabDrop(AppState& app)
{
    const int activeId = app.tabs[static_cast<size_t>(app.activeTab)].id;
    const int dragged  = TabById(app, app.dragTab);
    const int target   = app.tabDropTarget;
    const bool into    = app.tabDropInto;
    const bool after   = app.tabDropAfter;
    app.dragTab       = -1;
    app.tabPress      = -1;
    app.tabDropTarget = -1;

    if (dragged < 0 || target < 0 || target >= static_cast<int>(app.tabs.size()) ||
        target == dragged)
        return;

    if (into)
    {
        // Dentro un altro tab: si diventa suoi figli. Un tab non puo' finire dentro
        // un proprio discendente — sarebbe un anello, e un albero con un anello non
        // e' piu' un albero.
        if (TabIsDescendantOf(app, app.tabs[static_cast<size_t>(target)].id,
                              app.tabs[static_cast<size_t>(dragged)].id))
            return;
        app.tabs[static_cast<size_t>(dragged)].parentId =
            app.tabs[static_cast<size_t>(target)].id;
        return;
    }

    // Prima o dopo: si diventa **fratelli** del bersaglio. Fra fratelli l'ordine e'
    // l'ordine nel vettore, quindi qui il tab si sposta davvero nel vettore — con
    // l'identita' che resta, che e' quello che tiene in piedi i legami.
    Tab moved = app.tabs[static_cast<size_t>(dragged)];
    moved.parentId = app.tabs[static_cast<size_t>(target)].parentId;
    app.tabs.erase(app.tabs.begin() + dragged);
    int at = (target > dragged) ? target - 1 : target;
    if (after)
        ++at;
    at = (std::max)(0, (std::min)(at, static_cast<int>(app.tabs.size())));
    app.tabs.insert(app.tabs.begin() + at, moved);

    // L'indice attivo segue il **tab** che era attivo, non la sua posizione.
    const int stillActive = TabById(app, activeId);
    if (stillActive >= 0)
        app.activeTab = stillActive;
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

    std::vector<int> doomed;
    for (const Tab& tab : app.tabs)
    {
        if (TabIsDescendantOf(app, tab.id, root))
            doomed.push_back(tab.id);
    }
    // L'ultimo tab non si chiude: resterebbe una finestra senza niente dentro.
    // Si chiude tutto tranne uno, e si dice invece di far finta.
    const bool all = (doomed.size() == app.tabs.size());
    if (all)
        doomed.pop_back();

    // Dal fondo: chiudendo, gli indici sotto scivolano, e chi resta deve restare
    // valido.
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        CloseTab(app, TabById(app, *it));
}

// I fissaggi della shell: "Accesso rapido" di Esplora risorse, che e' una cartella
// dello spazio dei nomi come le altre. Si legge adesso e si rilegge a ogni
// aggiornamento, perche' l'utente puo' averci appena messo qualcosa.
//
// Di suo questo elenco puo' contenere cartelle che l'utente ha scelto, quindi e'
// fatto di nomi *suoi*: nelle prove si fotografa la barra laterale **sotto** questa
// sezione. Vedi `docs/05`.
void BuildQuickAccess(AppState& app)
{
    app.quickAccess.clear();

    const std::wstring location = ShellLocation::QuickAccessName();
    std::string error;
    if (!location.empty())
    {
        for (const ShellLocation::Item& item : ShellLocation::Read(location, &error))
        {
            if (item.location.empty())
                continue;
            Place place;
            place.label = ToUtf8(item.name);
            place.path  = item.location;
            app.quickAccess.push_back(std::move(place));
        }
    }

    // Nel registro va il motivo, non i nomi: i fissaggi della shell sono cartelle
    // scelte dall'utente. Vedi `AGENTS.md` §0.
    DiagLog("--- " + DiagClock() + " accesso rapido: " +
            (location.empty() ? std::string("la posizione non si apre")
                              : std::to_string(app.quickAccess.size()) + " voci" +
                                    (error.empty() ? "" : ", errore: " + error)));
}

// I volumi, con quanto sono pieni. La shell il nome di un volume lo sa scrivere —
// "Windows (C:)" — e la capacita' la da' il sistema: sono due chiamate, e si fanno
// quando si aggiorna la vista, non a ogni fotogramma.
void RefreshVolumes(AppState& app)
{
    app.volumes.clear();

    wchar_t letters[512]{};
    if (GetLogicalDriveStringsW(511, letters) == 0)
        return;

    for (const wchar_t* letter = letters; *letter != L'\0'; letter += wcslen(letter) + 1)
    {
        const UINT type = GetDriveTypeW(letter);
        // Un lettore senza disco non ha niente da mostrare, e una barra vuota
        // accanto a un'icona racconta una cosa che non c'e'.
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_REMOTE)
            continue;

        Place place;
        place.path = letter;

        SHFILEINFOW info{};
        if (SHGetFileInfoW(letter, 0, &info, sizeof(info), SHGFI_DISPLAYNAME) != 0 &&
            info.szDisplayName[0] != L'\0')
            place.label = ToUtf8(info.szDisplayName);
        else
            place.label = ToUtf8(letter);

        ULARGE_INTEGER available{};
        ULARGE_INTEGER total{};
        if (GetDiskFreeSpaceExW(letter, &available, &total, nullptr) &&
            total.QuadPart > 0)
        {
            place.usedFraction = 1.0f -
                static_cast<float>(static_cast<double>(available.QuadPart) /
                                   static_cast<double>(total.QuadPart));
            place.freeText = HumanBytes(available.QuadPart);
        }
        app.volumes.push_back(std::move(place));
    }
}

void BuildPlaces(AppState& app)
{
    BuildQuickAccess(app);
    RefreshVolumes(app);

    struct Known { KNOWNFOLDERID id; const char* label; };
    const Known known[] = {
        {FOLDERID_Profile,   "Home"},
        {FOLDERID_Desktop,   "Desktop"},
        {FOLDERID_Documents, "Documents"},
        {FOLDERID_Downloads, "Downloads"},
        {FOLDERID_Pictures,  "Pictures"},
        {FOLDERID_Music,     "Music"},
        {FOLDERID_Videos,    "Videos"},
    };

    for (const Known& item : known)
    {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(item.id, 0, nullptr, &path)) && path != nullptr)
        {
            app.places.push_back(Place{item.label, path});
            CoTaskMemFree(path);
        }
    }

    // Il cestino va **in fondo**, e non per ordine alfabetico: non e' una cartella in
    // cui si tiene roba, e' dove si va a prendere quello che si e' buttato. Si apre
    // come una cartella qualunque, perche' una cartella dello spazio dei nomi e' una
    // cartella.
    {
        const std::wstring bin = ShellLocation::ParsingNameOf(FOLDERID_RecycleBinFolder);
        app.recycleBinPath = bin;
        if (!bin.empty())
        {
            const std::wstring name = ShellLocation::DisplayName(bin);
            app.places.push_back(
                Place{ToUtf8(name.empty() ? std::wstring(L"Recycle Bin") : name), bin});
        }
    }
}

// --- layout ----------------------------------------------------------------
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

Layout ComputeLayout(const AppState& app)
{
    const float scale = app.zoom;
    const float width = static_cast<float>(app.viewportWidth);
    const float height = static_cast<float>(app.viewportHeight);

    const float sidebarWidth   = app.sidebarVisible ? 186.0f * scale : 0.0f;
    const float inspectorWidth = 292.0f * scale;
    const float titleHeight    = 34.0f * scale;
    const float toolbarHeight  = 38.0f * scale;
    // A grid view has no columns to head, so it gives the space back to the list.
    const bool gridView = (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons);
    const float headerHeight   = gridView ? 0.0f : 26.0f * scale;
    const float statusHeight   = 22.0f * scale;
    const float filterHeight   = 24.0f * scale;
    const float gap            = 6.0f * scale;

    Layout layout;
    layout.scale      = scale;
    // `Spacing` e' una preferenza: l'altezza della riga di lista la segue, e per
    // questo e' un valore derivato e non un 24 scritto a mano.
    layout.rowHeight  = (app.rowSpacing > 0.0f ? app.rowSpacing : 24.0f) * scale;
    layout.titleBar   = Rect{0.0f, 0.0f, width, titleHeight};
    layout.toolbar    = Rect{0.0f, titleHeight, width, toolbarHeight};
    layout.sidebar    = Rect{gap, titleHeight + toolbarHeight + gap, sidebarWidth,
                             height - titleHeight - toolbarHeight - statusHeight - gap * 3};
    layout.header     = Rect{sidebarWidth + gap * 2, titleHeight + toolbarHeight + gap,
                             width - sidebarWidth - inspectorWidth - gap * 4, headerHeight};
    layout.filterBar  = Rect{layout.header.x, height - statusHeight - gap - filterHeight,
                             layout.header.w, filterHeight};
    layout.list       = Rect{layout.header.x, layout.header.bottom(),
                             layout.header.w,
                             layout.filterBar.y - gap - layout.header.bottom()};
    layout.inspector  = Rect{width - inspectorWidth - gap, titleHeight + toolbarHeight + gap,
                             inspectorWidth, layout.list.h + headerHeight + filterHeight + gap};
    layout.statusBar  = Rect{0.0f, height - statusHeight, width, statusHeight};
    return layout;
}

// --- drawing ---------------------------------------------------------------
float TextY(const Layout& layout, const Rect& row, const FontAtlas& font)
{
    const float lineHeight = font.LineHeight() * layout.scale;
    return row.y + (row.h - lineHeight) * 0.5f;
}

// The window draws its own caption. The native one is removed by answering
// WM_NCCALCSIZE; what replaces it is a tab strip, which is the point of the
// milestone, plus the three window buttons and the menu button.
void DrawTitleBar(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.titleBar;

    r.FillRect(bar, t.headerBg);
    r.FillRect(Rect{bar.x, bar.bottom() - 1.0f, bar.w, 1.0f}, t.separator);

    const float buttonWidth = 44.0f * scale;
    const float stripLeft   = 44.0f * scale;
    const float stripRight  = bar.right() - buttonWidth * 3.0f;

    // Menu button. The reference opens the application menu here; the menu
    // component arrives with the command palette, so for now it toggles the one
    // thing the shell already has.
    const Rect menu{bar.x + 6.0f * scale, bar.y + 7.0f * scale, 30.0f * scale, bar.h - 14.0f * scale};
    r.FillRect(menu, app.hoveredChrome == 0 ? t.rowHover : t.panelBg, 5.0f * scale);
    for (int i = 0; i < 3; ++i)
        r.FillRect(Rect{menu.x + 8.0f * scale, menu.y + 5.0f * scale + i * 5.0f * scale,
                        14.0f * scale, 1.5f * scale}, t.text);
    {
        ClickTarget target;
        target.rect = menu;
        target.kind = ClickTarget::Kind::Menu;
        app.clickTargets.push_back(target);
    }

    float x = stripLeft;
    // La fila si ricava dall'albero: un figlio si disegna subito dopo il genitore,
    // rientrato. L'ordine di nascita non conta piu'.
    for (const std::pair<int, int>& node : TabOrder(app))
    {
        const int i     = node.first;
        const int depth = node.second;
        const Tab& tab = app.tabs[static_cast<size_t>(i)];
        // The active tab's snapshot is empty while it is live: its entries were
        // moved out and only the live fields are current, so the title has to come
        // from there.
        const std::wstring& source = (i == app.activeTab) ? app.directory : tab.directory;
        const std::string title = ToUtf8(LocationLabel(source));
        const float width = (std::min)(220.0f * scale,
                                       (std::max)(130.0f * scale,
                                                  app.font.Measure(title, scale) + 66.0f * scale));
        if (x + width > stripRight)
            break;   // no overflow menu yet: extra tabs are simply not drawn

        const Rect tabRect{x, bar.y + 5.0f * scale, width, bar.h - 5.0f * scale};
        const bool active = (i == app.activeTab);

        if (active)
        {
            r.FillRect(tabRect, t.panelBg, 6.0f * scale);
            r.Draw(UIDraw::Border(tabRect, t.panelBorder, 1.0f, 6.0f * scale));
        }
        else if (i == app.hoveredTab)
        {
            r.FillRect(tabRect, t.rowHover, 6.0f * scale);
        }

        // Il rientro di un figlio, con la riga che lo lega al genitore: senza, un
        // tab annidato e' indistinguibile da uno spostato.
        const float indent = static_cast<float>(depth) * 12.0f * scale;
        if (depth > 0)
        {
            r.FillRect(Rect{tabRect.x + indent - 8.0f * scale, tabRect.y + 6.0f * scale,
                            1.0f * scale, tabRect.h - 12.0f * scale},
                       t.separator);
        }

        if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor("", true))
        {
            const Rect icon{tabRect.x + 10.0f * scale + indent, tabRect.y + 6.0f * scale,
                            16.0f * scale, 16.0f * scale};
            r.Draw(UIDraw::Image(icon, active ? t.text : t.textDim, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        app.font.Draw(r, tabRect.x + 32.0f * scale + indent, TextY(layout, tabRect, app.font),
                      FitText(app.font, title, scale, tabRect.w - 56.0f * scale - indent),
                      active ? t.text : t.textDim, scale);

        const Rect close{tabRect.right() - 24.0f * scale, tabRect.y + 5.0f * scale,
                         18.0f * scale, tabRect.h - 10.0f * scale};
        app.smallFont.Draw(r, close.x + 5.0f * scale, TextY(layout, close, app.smallFont), "x",
                      i == app.hoveredTab ? t.text : t.textDim, scale);

        ClickTarget tabTarget;
        tabTarget.rect  = tabRect;
        tabTarget.kind  = ClickTarget::Kind::Tab;
        tabTarget.index = i;
        app.clickTargets.push_back(tabTarget);

        // Il marcatore del trascinamento: dove finirebbe il tab che si sta
        // spostando. La riga dice "prima o dopo", il tab intero dice "dentro" — e
        // non promette niente che il rilascio rifiuterebbe: dentro se stesso o
        // dentro un proprio discendente non si va, o l'albero prende un anello.
        if (app.dragTab >= 0 && app.tabDropTarget == i)
        {
            const bool refused = TabIsDescendantOf(app, tab.id, app.dragTab);
            if (app.tabDropInto)
            {
                if (!refused)
                    r.Draw(UIDraw::Fill(tabRect, t.accent.WithAlpha(0.18f), 6.0f * scale));
            }
            else
            {
                const float lineX = app.tabDropAfter ? tabRect.right() : tabRect.x;
                r.FillRect(Rect{lineX - 1.0f * scale, tabRect.y, 2.0f * scale, tabRect.h},
                           t.accent, 1.0f * scale);
            }
        }

        ClickTarget closeTarget;
        closeTarget.rect  = close;
        closeTarget.kind  = ClickTarget::Kind::TabClose;
        closeTarget.index = i;
        app.clickTargets.push_back(closeTarget);

        x += width + 3.0f * scale;
    }

    const Rect add{x, bar.y + 5.0f * scale, 30.0f * scale, bar.h - 5.0f * scale};
    if (add.right() <= stripRight)
    {
        r.FillRect(add, app.hoveredChrome == 1 ? t.rowHover : t.headerBg, 5.0f * scale);
        app.font.Draw(r, add.x + 11.0f * scale, TextY(layout, add, app.font), "+", t.text, scale);

        ClickTarget target;
        target.rect = add;
        target.kind = ClickTarget::Kind::TabNew;
        app.clickTargets.push_back(target);
    }

    const bool maximized = (app.window != nullptr) && IsZoomed(app.window) != FALSE;
    for (int i = 0; i < 3; ++i)
    {
        const Rect button{stripRight + i * buttonWidth, bar.y, buttonWidth, bar.h};
        const bool hot = (app.hoveredChrome == 2 + i);
        if (hot)
            r.FillRect(button, i == 2 ? Color::FromHex(0xC42B1C) : t.rowHover);

        const Color glyph = hot && i == 2 ? Color{1, 1, 1, 1} : t.textDim;
        const float centreX = button.x + buttonWidth * 0.5f;
        const float centreY = button.y + button.h * 0.5f;

        if (i == 0)
        {
            r.FillRect(Rect{centreX - 5.0f * scale, centreY, 10.0f * scale, 1.0f}, glyph);
        }
        else if (i == 1)
        {
            if (maximized)
            {
                r.Draw(UIDraw::Border(Rect{centreX - 4.0f * scale, centreY - 5.0f * scale,
                                           9.0f * scale, 9.0f * scale}, glyph, 1.0f));
                r.Draw(UIDraw::Border(Rect{centreX - 6.0f * scale, centreY - 3.0f * scale,
                                           9.0f * scale, 9.0f * scale}, glyph, 1.0f));
            }
            else
            {
                r.Draw(UIDraw::Border(Rect{centreX - 5.0f * scale, centreY - 5.0f * scale,
                                           10.0f * scale, 10.0f * scale}, glyph, 1.0f));
            }
        }
        else
        {
            app.smallFont.Draw(r, centreX - app.smallFont.Measure("x", scale) * 0.5f,
                          centreY - app.smallFont.LineHeight() * scale * 0.5f, "x", glyph, scale);
        }

        ClickTarget target;
        target.rect = button;
        target.kind = (i == 0) ? ClickTarget::Kind::WindowMinimize
                    : (i == 1) ? ClickTarget::Kind::WindowMaximize
                               : ClickTarget::Kind::WindowClose;
        app.clickTargets.push_back(target);
    }
}

void DrawToolbar(AppState& app, const Layout& layout, const std::wstring& displayPath)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.FillRect(layout.toolbar, t.headerBg);

    // Breadcrumb: split the path into components, each clickable. Everything is
    // measured from the toolbar rectangle, not from the top of the window: the
    // title bar above it is drawn by the application and its height varies with
    // the UI scale.
    // I pulsanti di navigazione, a sinistra del percorso: indietro, avanti, su,
    // aggiorna, e la stella dei segnalibri. Sono i più usati dopo il percorso, e in
    // File Pilot stanno li'.
    const char* const kNavGlyphs[5] = {"←", "→", "↑", "↻", "★"};
    const bool navEnabled[5] = {
        app.historyIndex > 0,
        app.historyIndex + 1 < static_cast<int>(app.history.size()),
        !app.directory.empty(),
        true,
        !app.directory.empty(),
    };
    constexpr float kNavSize = 26.0f;
    float navX = layout.toolbar.x + 8.0f * scale;
    for (int i = 0; i < 5; ++i)
    {
        const Rect button{navX, layout.toolbar.y + 5.0f * scale, kNavSize * scale,
                          kNavSize * scale};
        const bool bookmarked =
            (i == 4) && std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory) !=
                            app.bookmarks.end();
        if (i == app.hoveredNav && navEnabled[i])
            r.FillRect(button, t.rowHover, 5.0f * scale);
        else if (bookmarked)
            r.FillRect(button, t.accent.WithAlpha(0.18f), 5.0f * scale);

        // La stella e' piena quando la cartella e' nei segnalibri: e' l'unico segno
        // che dice se il comando aggiunge o toglie.
        const char* glyph = (i == 4 && !bookmarked) ? "☆" : kNavGlyphs[i];
        const Color color = !navEnabled[i] ? t.textDim.WithAlpha(0.4f)
                          : bookmarked      ? t.accent
                                            : t.textDim;
        const float glyphWidth = app.font.Measure(glyph, scale);
        app.font.Draw(r, button.x + (button.w - glyphWidth) * 0.5f,
                      TextY(layout, button, app.font), glyph, color, scale);

        ClickTarget target;
        target.rect  = button;
        target.kind  = ClickTarget::Kind::NavButton;
        target.index = i;
        app.clickTargets.push_back(target);

        navX += (kNavSize + 2.0f) * scale;
    }

    float penX = navX + 8.0f * scale;
    const float baseline = layout.toolbar.y + 11.0f * scale;

    std::wstring accumulated;
    size_t start = 0;
    while (start <= displayPath.size())
    {
        size_t slash = displayPath.find(L'\\', start);
        if (slash == std::wstring::npos)
            slash = displayPath.size();

        std::wstring segment = displayPath.substr(start, slash - start);
        if (accumulated.empty())
            accumulated = segment;
        else if (!segment.empty())
            accumulated += L"\\" + segment;

        const std::string label = ToUtf8(segment);
        const float width = app.font.Measure(label, scale) + 14.0f * scale;

        const Rect chip{penX, baseline - 4.0f * scale, width, 22.0f * scale};
        ClickTarget target;
        target.rect  = chip;
        target.kind  = ClickTarget::Kind::Breadcrumb;
        // In una cartella dello spazio dei nomi la briciola e' il nome che la
        // shell le da', che non e' una posizione: cliccarla deve riportare li', non
        // cercare una cartella chiamata "Cestino".
        target.path  = ShellLocation::IsNamespace(app.directory) ? app.directory : accumulated;
        app.clickTargets.push_back(target);

        if (!segment.empty())
        {
            app.font.Draw(r, penX + 7.0f * scale, baseline + 2.0f * scale, label,
                          slash == displayPath.size() ? t.text : t.textDim, scale);
        }
        penX += width;

        if (slash == displayPath.size())
            break;
        app.font.Draw(r, penX - 4.0f * scale, baseline + 2.0f * scale, ">", t.textDim, scale);
        start = slash + 1;
    }

    // Item count on the right.
    char summary[64];
    std::snprintf(summary, sizeof(summary), "%zu items", app.entries.size());
    const float summaryWidth = app.font.Measure(summary, scale);
    app.font.Draw(r, static_cast<float>(app.viewportWidth) - summaryWidth - 14.0f * scale,
                  layout.toolbar.y + 12.0f * scale, summary, t.textDim, scale);
}

// --- barra laterale ---------------------------------------------------------
//
// Quattro sezioni, nell'ordine in cui si vedono in File Pilot: i fissaggi della
// shell, i segnalibri, i volumi, le cartelle standard. Non e' una lista sola
// perche' le voci sono di natura diversa — e perche' i segnalibri li fa l'utente,
// quindi la sezione deve poter stare vuota senza sparire.
//
// Le intestazioni sono in inglese come nell'originale: sono i suoi nomi
// (`QuickAccess`, `Bookmarks`, `Storage`, `Places` nella tabella delle chiavi),
// non parole nostre da tradurre.

struct SidebarRow
{
    bool         header = false;
    std::string  title;     // per un'intestazione: il nome della sezione
    std::string  label;
    std::wstring path;
    bool         volume = false;
    float        usedFraction = 0.0f;
    std::string  freeText;
};

void BuildSidebarRows(const AppState& app, std::vector<SidebarRow>* rows)
{
    const auto section = [&](const char* title, const std::vector<Place>& places) {
        SidebarRow header;
        header.header = true;
        header.title  = title;
        rows->push_back(std::move(header));

        if (app.collapsedSections.count(title) != 0)
            return;
        for (const Place& place : places)
        {
            SidebarRow row;
            row.label        = place.label;
            row.path         = place.path;
            row.freeText     = place.freeText;
            row.usedFraction = place.usedFraction;
            row.volume       = !place.freeText.empty();
            rows->push_back(std::move(row));
        }
    };

    // QUICK ACCESS si mostra solo se ha qualcosa dentro: una sezione vuota qui non
    // vuol dire "non ne hai fissata nessuna", vuol dire che la cartella della shell
    // non si e' letta — e una sezione vuota senza spiegazione non si capisce.
    if (!app.quickAccess.empty())
        section("QUICK ACCESS", app.quickAccess);

    std::vector<Place> marks;
    marks.reserve(app.bookmarks.size());
    for (const std::wstring& path : app.bookmarks)
        marks.push_back(Place{ToUtf8(LeafName(path)), path});
    section("BOOKMARKS", marks);

    section("STORAGE", app.volumes);
    section("PLACES", app.places);
}

void DrawSidebar(AppState& app, const Layout& layout)
{
    if (!app.sidebarVisible)
        return;

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.Draw(UIDraw::Fill(layout.sidebar, t.panelBg, 6.0f * scale));

    std::vector<SidebarRow> rows;
    BuildSidebarRows(app, &rows);

    float y = layout.sidebar.y + 8.0f * scale;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const SidebarRow& item = rows[i];
        const float rowHeight = (item.header ? 24.0f : (item.volume ? 46.0f : 26.0f)) * scale;
        const Rect row{layout.sidebar.x + 5.0f * scale, y, layout.sidebar.w - 10.0f * scale,
                       rowHeight};

        if (item.header)
        {
            const bool collapsed = app.collapsedSections.count(item.title) != 0;
            if (static_cast<int>(i) == app.hoveredSection)
                r.FillRect(row, t.rowHover, 4.0f * scale);

            // Il verso del triangolino e' lo stato: chiuso punta a destra, aperto in
            // giu'. E' l'unico segno che dice che la sezione si puo' chiudere.
            app.smallFont.Draw(r, row.x + 6.0f * scale, row.y + 5.0f * scale,
                               collapsed ? ">" : "v", t.textDim, scale);
            app.smallFont.Draw(r, row.x + 20.0f * scale, row.y + 5.0f * scale, item.title,
                               t.textDim, scale);

            ClickTarget target;
            target.rect  = row;
            target.kind  = ClickTarget::Kind::SidebarHeader;
            target.label = item.title;
            app.clickTargets.push_back(target);
            y += rowHeight;
            continue;
        }

        const bool active = (item.path == app.directory);
        if (active)
            r.FillRect(row, Color::FromHex(0x2E3138), 4.0f * scale);
        else if (static_cast<int>(i) == app.hoveredPlace)
            r.FillRect(row, t.rowHover, 4.0f * scale);

        // La voce su cui e' in corso una caduta: lo stesso tratteggio delle righe,
        // perche' e' la stessa cosa detta in un altro posto.
        if (static_cast<int>(i) == app.dropTargetSidebarRow)
            r.Draw(UIDraw::DashedBorder(row, t.accent, 2.0f * scale, 6.0f * scale, 4.0f * scale,
                                        4.0f * scale));

        // L'icona di un volume e' quella che la shell da' all'unita': un disco, non
        // una cartella. Le altre voci sono cartelle.
        const ShellIconAtlas::IconUV* uv = app.icons.GetFor("", true);
        if (uv != nullptr)
        {
            const Rect icon{row.x + 8.0f * scale, row.y + 5.0f * scale, 16.0f * scale, 16.0f * scale};
            r.Draw(UIDraw::Image(icon, active ? t.text : t.textDim, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }
        app.font.Draw(r, row.x + 30.0f * scale, row.y + 5.0f * scale, item.label,
                      active ? t.text : t.textDim, scale);

        if (item.volume)
        {
            // Barra di occupazione e spazio libero: due informazioni in un posto
            // solo, come le mostra Esplora risorse. La barra si colora quando il
            // disco e' quasi pieno, perche' e' quello il momento in cui la si guarda.
            const Rect track{row.x + 30.0f * scale, row.y + 26.0f * scale,
                             row.w - 42.0f * scale, 4.0f * scale};
            r.FillRect(track, Color::FromHex(0x2A2D34), 2.0f * scale);
            const Color fill = item.usedFraction > 0.9f ? Color::FromHex(0xE06C5A)
                                                       : t.accent;
            r.FillRect(Rect{track.x, track.y, track.w * (std::min)(1.0f, item.usedFraction),
                            track.h},
                       fill, 2.0f * scale);
            app.smallFont.Draw(r, row.x + 30.0f * scale, row.y + 31.0f * scale,
                               item.freeText + " free", t.textDim, scale);
        }

        ClickTarget target;
        target.rect = row;
        target.kind = ClickTarget::Kind::Place;
        target.index = static_cast<int>(i);
        target.path = item.path;
        app.clickTargets.push_back(target);

        y += rowHeight;
    }
}

// La larghezza di partenza e' quella di `kColumns`. Si chiama una volta, quando
// l'applicazione nasce: dopo, la larghezza e' quella che l'utente ha scelto.
void InitColumns(AppState& app)
{
    for (int i = 0; i < kColumnCount; ++i)
        app.columns[i].fraction = kColumns[i].fraction;
}

void GroupMenuOpen(GroupMenu& menu, std::string title, std::string placeholder,
                   std::vector<GroupMenuItem> items);

// L'etichetta italiana di un comando. La tabella resta com'e' — con le sue
// etichette in inglese, che sono anche i nomi delle chiavi del riferimento — e la
// traduzione sta qui, accanto, in un posto solo.
//
// Se un comando non compare, la sua etichetta resta quella della tabella: mancare
// una traduzione non deve rompere niente, e si vede subito a schermo. La lista e'
// tenuta corta apposta, cosi' "cosa non e' tradotto" ha una risposta che si legge.
const char* CommandLabelIt(CommandId id)
{
    switch (id)
    {
        case CmdNewTab:                 return "Nuova scheda";
        case CmdCloseTab:               return "Chiudi scheda";
        case CmdNextTab:                return "Scheda successiva";
        case CmdPrevTab:                return "Scheda precedente";
        case CmdCloseInTree:            return "Chiudi scheda e figli";
        case CmdCloseAllInTree:         return "Chiudi tutto l'albero";
        case CmdDetachTab:              return "Stacca la scheda dal genitore";
        case CmdGoUp:                   return "Vai alla cartella superiore";
        case CmdBack:                   return "Indietro";
        case CmdForward:                return "Avanti";
        case CmdRefresh:                return "Aggiorna";
        case CmdRecalcStats:            return "Ricalcola le dimensioni";
        case CmdOpenExplorer:           return "Apri in Esplora risorse";
        case CmdOpenConsole:            return "Apri un terminale qui";
        case CmdCopy:                   return "Copia";
        case CmdCut:                    return "Taglia";
        case CmdPaste:                  return "Incolla";
        case CmdRename:                 return "Rinomina";
        case CmdNewFolder:              return "Nuova cartella";
        case CmdDelete:                 return "Elimina (nel cestino)";
        case CmdDeletePermanent:        return "Elimina definitivamente";
        case CmdGoTo:                   return "Vai a una cartella...";
        case CmdViewDetails:            return "Vista: dettagli";
        case CmdViewList:               return "Vista: elenco";
        case CmdViewTiles:              return "Vista: riquadri";
        case CmdViewIcons:              return "Vista: icone";
        case CmdToggleSidebar:          return "Mostra la barra laterale";
        case CmdToggleHoverToSelect:    return "Il passaggio seleziona";
        case CmdToggleUnderlineOnHover: return "Sottolinea al passaggio";
        case CmdToggleTooltips:         return "Mostra i suggerimenti";
        case CmdSingleClick:            return "Un clic apre";
        case CmdSingleClickFoldersOnly: return "Un clic: solo le cartelle";
        case CmdActOnPress:             return "Agisci premendo";
        case CmdDoubleClickToParent:    return "Doppio clic sul fondo: su di un livello";
        case CmdShowCheckboxes:         return "Mostra le caselle di scelta";
        case CmdZoomIn:                 return "Ingrandisci";
        case CmdZoomOut:                return "Rimpicciolisci";
        case CmdZoomReset:              return "Ingrandimento normale";
        case CmdTextWhitespace:         return "Testo: segna spazi e tabulazioni";
        case CmdTextLineNumbers:        return "Testo: numeri di riga";
        case CmdTextMinimap:            return "Testo: minimappa";
        case CmdTextWrap:               return "Testo: vai a capo";
        case CmdTextZoomIn:             return "Testo: ingrandisci";
        case CmdTextZoomOut:            return "Testo: rimpicciolisci";
        case CmdImageZoomIn:            return "Immagine: ingrandisci";
        case CmdImageZoomOut:           return "Immagine: rimpicciolisci";
        case CmdImageZoomFit:           return "Immagine: adatta al pannello";
        case CmdToggleDetails:          return "Palette: mostra i dettagli";
        case CmdSortName:               return "Ordina per nome";
        case CmdSortType:               return "Ordina per tipo";
        case CmdSortModified:           return "Ordina per data";
        case CmdSortFolders:            return "Ordina per sottocartelle";
        case CmdSortFiles:              return "Ordina per file";
        case CmdSortSize:               return "Ordina per dimensione";
        case CmdSortDirection:          return "Inverti l'ordine";
        case CmdGroupNone:              return "Raggruppa: no";
        case CmdGroupName:              return "Raggruppa per nome";
        case CmdGroupDate:              return "Raggruppa per data";
        case CmdGroupSize:              return "Raggruppa per dimensione";
        case CmdGroupCount:             return "Raggruppa per numero di voci";
        case CmdGroupVolume:            return "Raggruppa per occupazione del disco";
        case CmdFocusFilter:            return "Vai al filtro";
        case CmdClearFilter:            return "Azzera il filtro";
        case CmdScrollTop:              return "Vai in cima";
        case CmdScrollBottom:           return "Vai in fondo";
        case CmdToggleBookmark:         return "Aggiungi questo posto ai segnalibri";
        case CmdClearBookmarks:         return "Togli tutti i segnalibri";
        case CmdClearRecents:           return "Azzera i recenti";
        case CmdLanguageItalian:        return "Lingua: italiano";
        case CmdLanguageEnglish:        return "Lingua: inglese";
        case CmdShowSettingsPath:       return "Dove sta il file delle impostazioni";
        case CmdOpenOptions:            return "Apri le opzioni";
        default:                        return nullptr;
    }
}

// Il pannello dei valori di una colonna. I valori si contano **all'apertura** e
// non si riordinano mai: togliendone uno, le caselle non devono saltare sotto il
// puntatore. Per la stessa ragione l'ordine e' per frequenza decrescente, che non
// cambia mentre si spunta.
constexpr int kFilterAllId  = -1;
constexpr int kFilterNoneId = -2;

void BuildColumnMenu(AppState& app, int column)
{
    // Il pannello si appoggia al bordo sinistro della sua colonna: e' cosi' che si
    // vede a quale intestazione appartiene, senza doverlo scrivere.
    const Layout layout = ComputeLayout(app);
    app.columnMenu.anchor = Rect{layout.header.x + layout.header.w *
                                     app.columns[static_cast<size_t>(column)].fraction,
                                 layout.header.y, 0.0f, layout.header.h};

    std::vector<std::pair<std::string, int>> counts;
    std::unordered_map<std::string, int> index;
    for (const Entry& entry : app.entries)
    {
        const std::string value = ColumnValueOf(app, entry, column);
        if (value.empty())
            continue;
        auto found = index.find(value);
        if (found == index.end())
        {
            index[value] = static_cast<int>(counts.size());
            counts.push_back({value, 0});
        }
        counts[static_cast<size_t>(index[value])].second += 1;
    }

    std::sort(counts.begin(), counts.end(),
              [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                  if (a.second != b.second)
                      return a.second > b.second;
                  return a.first < b.first;
              });
    if (counts.size() > 40)
        counts.resize(40);   // dichiarato: oltre, l'elenco non si legge piu'

    app.columnPanelValues = std::move(counts);
    app.columnPanel       = column;

    std::vector<GroupMenuItem> items;
    items.push_back({"", "Tutti", "", kFilterAllId});
    items.push_back({"", "Nessuno", "", kFilterNoneId});
    for (int i = 0; i < static_cast<int>(app.columnPanelValues.size()); ++i)
    {
        const std::pair<std::string, int>& value = app.columnPanelValues[static_cast<size_t>(i)];
        const bool kept = app.columns[static_cast<size_t>(column)].excluded.count(value.first) == 0;
        items.push_back({"", (kept ? "[x] " : "[ ] ") + value.first,
                         std::to_string(value.second), i});
    }

    app.columnMenu.detailed = true;
    GroupMenuOpen(app.columnMenu, kColumns[column].label, "Type to filter values...",
                  std::move(items));
    app.columnMenu.anchored = true;
}

// Rimette le caselle al loro posto dopo una spunta, senza ricontare e senza
// riordinare: cambia solo il segno, e i valori restano quelli di quando il
// pannello si e' aperto.
void RefreshColumnMenuChecks(AppState& app)
{
    if (app.columnPanel < 0)
        return;
    for (GroupMenuItem& item : app.columnMenu.items)
    {
        if (item.id < 0)
            continue;
        const std::pair<std::string, int>& value = app.columnPanelValues[static_cast<size_t>(item.id)];
        const bool kept = app.columns[static_cast<size_t>(app.columnPanel)].excluded.count(value.first) == 0;
        item.label = (kept ? "[x] " : "[ ] ") + value.first;
    }
}

// La casella di scelta, quando `ShowCheckboxes` e' acceso. Riflette la selezione:
// una casella accanto a una riga non selezionata sarebbe una seconda verita' da
// tenere allineata alla prima, e prima o poi le due divergono.
//
// Si preme per scegliere: la casella prende il clic, la riga sotto no. E' il gesto
// che serve a scegliere senza aprire, che e' tutto il motivo per cui le caselle
// esistono accanto al clic singolo.
float DrawCheckbox(AppState& app, const Rect& row, bool selected, float scale,
                   float padding, int entryIndex)
{
    const Theme& t = app.theme;
    const float side = 14.0f * scale;
    const Rect box{row.x + padding, row.y + (row.h - side) * 0.5f, side, side};
    app.renderer.Draw(UIDraw::Fill(box, selected ? t.accent : t.windowBg, 3.0f * scale));
    // Il bordo di una casella vuota e' in tono spento ma **visibile**: su una riga
    // scura un bordo del colore del pannello sparisce, e una casella che non si vede
    // e' una casella che non c'e'.
    app.renderer.Draw(UIDraw::Border(box, selected ? t.accent : t.textDim, 1.0f, 3.0f * scale));
    if (selected)
    {
        // Il segno di spunta e' un quadrato pieno: nell'atlante dei glifi non c'e'
        // un carattere di spunta, e scriverne uno fuori dall'insieme rasterizzato
        // darebbe un buco (§7.32).
        const float inner = 6.0f * scale;
        app.renderer.Draw(UIDraw::Fill(Rect{box.x + (side - inner) * 0.5f,
                                            box.y + (side - inner) * 0.5f, inner, inner},
                                       t.windowBg, 1.0f * scale));
    }

    ClickTarget target;
    target.rect  = Rect{box.x - 3.0f * scale, row.y, box.w + 6.0f * scale, row.h};
    target.kind  = ClickTarget::Kind::Checkbox;
    target.index = entryIndex;
    app.clickTargets.push_back(target);
    return box.w + padding;
}

void DrawHeader(AppState& app, const Layout& layout, float* columnX)
{
    if (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons)
        return;   // nothing to head, and the list already has the space

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.FillRect(layout.header, t.headerBg, 4.0f * scale);

    // A small pulse beside the statistics headers while jobs are running: the
    // numbers are still moving, and that is worth saying out loud.
    const float pulse = 0.25f + 0.75f * static_cast<float>(std::fabs(std::sin(app.lastFrameTime * 3.0)));

    for (int i = 0; i < kColumnCount; ++i)
    {
        const float x = layout.header.x + 10.0f * scale + layout.header.w * app.columns[i].fraction;
        columnX[i] = x;

        const bool sorted = (app.sortColumn == kColumns[i].sort);
        std::string label = kColumns[i].label;
        if (sorted)
            label += app.sortAscending ? " ^" : " v";

        // Il titolo si accorcia se non ci sta. Con il carattere di partenza ci
        // stava per fortuna; con un carattere piu' largo scelto dalle opzioni (o
        // con una colonna stretta trascinata) i titoli si sovrapponevano, e un
        // titolo sopra un altro e' peggio di un titolo troncato.
        const float nextX = layout.header.x + 10.0f * scale + layout.header.w *
                            (i + 1 < kColumnCount ? app.columns[static_cast<size_t>(i) + 1].fraction
                                                  : 1.0f);
        const float room = nextX - x - (kColumns[i].filterable ? 26.0f * scale : 8.0f * scale);
        if (room > 10.0f * scale)
            label = FitText(app.smallFont, label, scale, room);

        app.smallFont.Draw(r, x, TextY(layout, layout.header, app.smallFont), label,
                      sorted ? t.text : t.textDim, scale);

        if (i >= 3 && app.stats.Busy())
        {
            const float dotX = x + app.smallFont.Measure(label, scale) + 6.0f * scale;
            const float dotY = layout.header.y + layout.header.h * 0.5f - 2.0f * scale;
            r.FillRect(Rect{dotX, dotY, 4.0f * scale, 4.0f * scale},
                       t.accent.WithAlpha(pulse), 2.0f * scale);
        }

        const float width = (i < kColumnCount - 1)
            ? layout.header.w * (app.columns[i + 1].fraction - app.columns[i].fraction)
            : layout.header.w * (1.0f - app.columns[i].fraction);
        ClickTarget target;
        target.rect = Rect{x - 10.0f * scale, layout.header.y, width, layout.header.h};
        target.kind = ClickTarget::Kind::Column;
        target.index = i;
        app.clickTargets.push_back(target);

        // La linguetta del filtro: tre barre di larghezza decrescente, che e' il
        // segno che si legge come "imbuto". In colore d'accento quando la colonna
        // filtra davvero, spenta quando no.
        if (kColumns[i].filterable)
        {
            const float right = layout.header.x + layout.header.w *
                                (i + 1 < kColumnCount ? app.columns[i + 1].fraction : 1.0f);
            const float glyphX = right - 16.0f * scale;
            const float glyphY = layout.header.y + layout.header.h * 0.5f;
            const bool  filtered = !app.columns[static_cast<size_t>(i)].excluded.empty();
            const Color ink = filtered ? t.accent : t.textDim;
            const float widths[3] = {8.0f, 5.5f, 3.0f};
            for (int bar = 0; bar < 3; ++bar)
            {
                const float w = widths[bar] * scale;
                r.FillRect(Rect{glyphX + (8.0f * scale - w) * 0.5f,
                                glyphY - 4.0f * scale + bar * 3.0f * scale, w, 1.5f * scale},
                           ink, 0.5f * scale);
            }

            ClickTarget filterTarget;
            filterTarget.rect  = Rect{glyphX - 3.0f * scale, layout.header.y, 16.0f * scale,
                                      layout.header.h};
            filterTarget.kind  = ClickTarget::Kind::ColumnFilter;
            filterTarget.index = i;
            app.clickTargets.push_back(filterTarget);
        }

        // Il bordo, che si trascina. Sta a **sinistra** del nome della colonna: cosi'
        // il clic sul nome resta un clic per ordinare, e la striscia di presa non
        // copre niente che non sia gia' una zona morta.
        if (i > 0)
        {
            const float edge = layout.header.x + layout.header.w * app.columns[i].fraction;
            ClickTarget grip;
            grip.rect = Rect{edge - 3.0f * scale, layout.header.y, 6.0f * scale,
                             layout.header.h};
            grip.kind = ClickTarget::Kind::ColumnEdge;
            grip.index = i;
            app.clickTargets.push_back(grip);
        }
    }

    r.FillRect(Rect{layout.header.x, layout.header.bottom() - 1.0f, layout.header.w, 1.0f}, t.separator);
}

// The filter box sits under the list, the way the reference application puts it.
// It is the only text entry in the window, so it owns the keyboard whenever the
// filter is active.
GroupMenu* OpenMenu(AppState& app);

// `ShowTooltips`: il nome per esteso e due righe di contesto, vicino al puntatore.
//
// Compare **dopo una pausa**, come i suggerimenti del sistema: il conto riparte
// quando il puntatore si muove, quindi chi sta scorrendo l'elenco non se li vede
// passare davanti. Non e' un menu: non prende clic, e non si puo' navigare.
void DrawTooltip(AppState& app, const Layout& layout)
{
    if (!app.showTooltips || app.hovered < 0 || app.columnDrag >= 0 || OpenMenu(app) != nullptr ||
        app.contextMenu.open)
        return;
    if (NowSeconds() - app.hoverStart < 0.55)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(app.hovered)];
    const float scale = layout.scale;
    const float pad = 9.0f * scale;

    std::vector<std::string> lines;
    lines.push_back(entry.name);
    std::string second = entry.typeText;
    if (!entry.modifiedText.empty())
        second += "   " + entry.modifiedText;
    if (!entry.isDirectory && !entry.sizeText.empty())
        second += "   " + entry.sizeText;
    if (!second.empty())
        lines.push_back(second);
    if (entry.isDirectory)
    {
        DirStats stats;
        if (app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats) && !stats.partial)
            lines.push_back(HumanCount(stats.folders) + " cartelle   " +
                            HumanCount(stats.files) + " file   " +
                            HumanBytes(stats.logicalBytes));
    }

    float width = 0.0f;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const float measured = i == 0 ? app.font.Measure(lines[i], scale)
                                      : app.smallFont.Measure(lines[i], scale);
        width = (std::max)(width, measured);
    }
    const float height = app.font.LineHeight() * scale + 3.0f * scale +
                         static_cast<float>(lines.size() - 1) * app.smallFont.LineHeight() * scale +
                         pad * 2.0f;

    float x = app.pointerX + 16.0f * scale;
    float y = app.pointerY + 20.0f * scale;
    x = (std::min)(x, static_cast<float>(app.viewportWidth) - width - pad * 2.0f - 8.0f * scale);
    y = (std::min)(y, static_cast<float>(app.viewportHeight) - height - 8.0f * scale);
    const Rect box{(std::max)(8.0f * scale, x), (std::max)(8.0f * scale, y),
                   width + pad * 2.0f, height};

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    r.Draw(UIDraw::Shadow(Rect{box.x + 2.0f * scale, box.y + 4.0f * scale, box.w, box.h},
                          Color{0.0f, 0.0f, 0.0f, 0.45f}, 16.0f * scale, 6.0f * scale));
    r.Draw(UIDraw::Fill(box, t.panelBg, 6.0f * scale));
    r.Draw(UIDraw::Border(box, t.panelBorder, 1.0f, 6.0f * scale));

    float textY = box.y + pad;
    app.font.Draw(r, box.x + pad, textY, lines[0], t.text, scale);
    textY += app.font.LineHeight() * scale + 3.0f * scale;
    for (size_t i = 1; i < lines.size(); ++i)
    {
        app.smallFont.Draw(r, box.x + pad, textY, lines[i], t.textDim, scale);
        textY += app.smallFont.LineHeight() * scale;
    }
}

void SetViewMode(AppState& app, ViewMode mode)
{
    if (app.viewMode == mode)
        return;
    app.viewMode    = mode;
    app.viewDirty   = true;
    app.hovered     = -1;
    app.hoverStart  = NowSeconds();

    // I due comandi a griglia portano la misura ai due punti noti: sono scorciatoie,
    // non due modalita' separate.
    if (mode == ViewMode::Icons)
        app.gridIconSize = 64.0f;
    else if (mode == ViewMode::Tiles)
        app.gridIconSize = 128.0f;
}

void DrawFilterBar(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.filterBar;

    r.FillRect(bar, t.headerBg, 4.0f * scale);
    r.Draw(UIDraw::Border(bar, app.filterActive ? t.accent.WithAlpha(0.75f) : t.panelBorder,
                          1.0f, 4.0f * scale));

    const std::string text = app.filter.empty()
        ? ("Filter " + std::to_string(app.entries.size()) + " files...")
        : ToUtf8(app.filter);

    const float textX = bar.x + 10.0f * scale;
    app.font.Draw(r, textX, TextY(layout, bar, app.font), text,
                  app.filter.empty() ? t.textDim : t.text, scale);

    if (app.filterActive)
    {
        const float caretX = textX + app.font.Measure(text, scale) + 1.0f * scale;
        r.FillRect(Rect{caretX, bar.y + 5.0f * scale, 1.0f, bar.h - 10.0f * scale}, t.text);
    }

    // The count is how many entries the filter matched, not how many rows are on
    // screen: folding a group away must not look like the filter changed.
    const std::string matchText = std::to_string(app.matchCount) + " / " +
                                  std::to_string(app.entries.size());

    std::string groupText = std::string("Group: ") + GroupModeName(app.groupMode);
    const float matchWidth = app.smallFont.Measure(matchText, scale);
    const float groupWidth = app.smallFont.Measure(groupText, scale);
    const float rightEdge  = bar.right() - 10.0f * scale;

    app.smallFont.Draw(r, rightEdge - matchWidth, TextY(layout, bar, app.smallFont),
                  matchText, t.textDim, scale);

    const Rect groupRect{rightEdge - matchWidth - groupWidth - 20.0f * scale, bar.y,
                         groupWidth + 12.0f * scale, bar.h};
    // Il comando si accende quando il raggruppamento sta facendo qualcosa: adesso
    // anche nella griglia, che non e' piu' l'eccezione.
    app.smallFont.Draw(r, groupRect.x, TextY(layout, bar, app.smallFont), groupText,
                  app.groupMode != GroupMode::None ? t.accent : t.textDim, scale);

    ClickTarget target;
    target.rect = groupRect;
    target.kind = ClickTarget::Kind::GroupMode;
    app.clickTargets.push_back(target);
}

ViewMetrics ComputeViewMetrics(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    // The cell height is derived from the icon and the label rather than fixed,
    // so a two line name can never overlap the picture above it.
    const float labelHeight = app.font.LineHeight() * scale * 2.0f;

    ViewMetrics metrics;
    metrics.padding = 6.0f * scale;

    switch (app.viewMode)
    {
        case ViewMode::List:
            metrics.rowHeight = 20.0f * scale;
            metrics.iconSize  = 14.0f * scale;
            break;

        // Le due viste a griglia condividono la misura: quello che cambia e' solo
        // quanto e' grande. Tutto il resto si **deriva** da li' — la cella deve
        // contenere l'icona e stare comoda per l'etichetta, e un numero derivato
        // non puo' entrare in disaccordo con le sue parti.
        case ViewMode::Icons:
        case ViewMode::Tiles:
            metrics.grid       = true;
            metrics.iconSize   = app.gridIconSize * scale;
            metrics.cellWidth  = (app.gridIconSize + 40.0f) * scale;
            metrics.cellHeight = metrics.iconSize + labelHeight + 16.0f * scale;
            break;

        case ViewMode::Details:
        default:
            metrics.rowHeight = layout.rowHeight;
            metrics.iconSize  = 16.0f * scale;
            break;
    }

    if (metrics.grid)
    {
        const float usable = layout.list.w - metrics.padding * 2.0f;
        metrics.columns = (std::max)(1, static_cast<int>(usable / metrics.cellWidth));
        // Spread the leftover width so the cells tile the panel exactly.
        metrics.cellWidth = usable / static_cast<float>(metrics.columns);
        // Una banda di gruppo e' alta come una riga dell'elenco: e' la stessa cosa
        // detta in un'altra vista, e deve leggersi come la stessa cosa.
        metrics.bandHeight = layout.rowHeight;
    }
    return metrics;
}

// La geometria della griglia, **calcolata una volta** e usata da tutti.
//
// Prima ogni indice aveva la sua formula (`indice % colonne`, `indice / colonne`),
// e con le bande di gruppo quella formula non vale piu': una banda a piena
// larghezza spezza il flusso, e la riga di celle sotto di lei ricomincia da
// sinistra. Rifare il conto a ogni domanda vorrebbe dire farlo per ogni cella
// disegnata, per ogni clic e per ogni scorrimento, e tre conti che devono
// coincidere prima o poi non coincidono.
//
// Non c'e' lo scorrimento dentro questi rettangoli: chi li usa lo sottrae, cosi'
// la geometria non va rifatta a ogni rotella.
const std::vector<Rect>& GridRects(AppState& app, const Layout& layout)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    if (app.gridRectsRows == app.rows.size() && app.gridRectsWidth == layout.list.w &&
        app.gridRectsView == static_cast<int>(app.viewMode) &&
        app.gridRectsGroup == static_cast<int>(app.groupMode) && app.gridRectsScale == layout.scale &&
        app.gridRectsIcon == app.gridIconSize)
        return app.gridRects;

    app.gridRects.clear();
    app.gridRects.reserve(app.rows.size());

    const float left = layout.list.x + metrics.padding;
    float x = left;
    float y = layout.list.y + metrics.padding;
    int column = 0;

    for (const Row& row : app.rows)
    {
        if (row.header)
        {
            // Una cella a meta' riga non si lascia dietro una banda: la riga in
            // corso si chiude prima, e la banda parte da sinistra.
            if (column != 0)
            {
                y += metrics.cellHeight;
                column = 0;
                x = left;
            }
            app.gridRects.push_back(Rect{layout.list.x + 1.0f, y, layout.list.w - 2.0f,
                                         metrics.bandHeight});
            y += metrics.bandHeight;
            continue;
        }

        app.gridRects.push_back(Rect{x, y, metrics.cellWidth, metrics.cellHeight});
        x += metrics.cellWidth;
        if (++column >= metrics.columns)
        {
            column = 0;
            x = left;
            y += metrics.cellHeight;
        }
    }

    app.gridRectsRows  = app.rows.size();
    app.gridRectsWidth = layout.list.w;
    app.gridRectsView  = static_cast<int>(app.viewMode);
    app.gridRectsGroup = static_cast<int>(app.groupMode);
    app.gridRectsScale = layout.scale;
    app.gridRectsIcon  = app.gridIconSize;
    return app.gridRects;
}

float ViewContentHeight(AppState& app, const Layout& layout)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    if (!metrics.grid)
        return static_cast<float>(app.rows.size()) * metrics.rowHeight;
    const std::vector<Rect>& rects = GridRects(app, layout);
    if (rects.empty())
        return metrics.padding * 2.0f;
    return rects.back().bottom() - layout.list.y + metrics.padding;
}

Rect ItemRect(AppState& app, const Layout& layout, int rowIndex, float scroll)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    Rect rect;
    if (!metrics.grid)
    {
        rect = Rect{layout.list.x + 1.0f,
                    layout.list.y + static_cast<float>(rowIndex) * metrics.rowHeight,
                    layout.list.w - 2.0f, metrics.rowHeight};
    }
    else
    {
        const std::vector<Rect>& rects = GridRects(app, layout);
        if (rowIndex < 0 || rowIndex >= static_cast<int>(rects.size()))
            return Rect{};
        rect = rects[static_cast<size_t>(rowIndex)];
    }
    rect.y -= scroll;
    return rect;
}

// Splits a name into at most |maxLines| lines that fit, ellipsising the last one.
// Character based: this is a label, and the glyph atlas is ASCII.
std::vector<std::string> WrapName(const FontAtlas& font, const std::string& text, float scale,
                                  float maxWidth, int maxLines)
{
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0f || maxLines < 1)
        return lines;

    std::string current;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (font.Measure(current + c, scale) > maxWidth && !current.empty())
        {
            lines.push_back(current);
            if (static_cast<int>(lines.size()) == maxLines - 1)
            {
                current = text.substr(i);   // the rest goes on the last line
                break;
            }
            current.clear();
            if (c == ' ')
                continue;                    // do not start a line with a space
        }
        current.push_back(c);
    }
    if (!current.empty())
        lines.push_back(current);

    if (!lines.empty())
        lines.back() = FitText(font, lines.back(), scale, maxWidth);
    return lines;
}

// One cell of a grid view: thumbnail on top, name underneath, centred.
void DrawEntryCell(AppState& app, const Layout& layout, const ViewMetrics& metrics,
                   const Rect& cell, const Row& view, int cursorIndex)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Entry& entry = app.entries[static_cast<size_t>(view.entry)];

    const Rect card = cell.Inset(metrics.padding * 0.5f, metrics.padding * 0.5f);
    // Nella griglia il contenuto e' centrato, quindi la casella sta nell'angolo
    // senza spostare niente.
    const bool checkboxes = app.showCheckboxes;
    // La selezione vince sul sovrappasso: la riga sotto il mouse resta leggibile
    // anche quando e' selezionata, invece di cambiare colore sotto il puntatore.
    if (IsEntrySelected(app, entry))
        r.FillRect(card, t.rowSelected, 4.0f * scale);
    else if (view.entry == app.hovered)
        r.FillRect(card, t.rowHover, 4.0f * scale);
    if (view.entry == cursorIndex)
        r.Draw(UIDraw::Border(card, t.accent.WithAlpha(0.9f), 1.0f, 4.0f * scale));

    if (checkboxes)
        DrawCheckbox(app, cell, IsEntrySelected(app, entry), scale, metrics.padding * 0.5f,
                     view.entry);

    // La destinazione di una caduta: tratteggio, cosi' si distingue sia dal
    // riempimento della selezione sia dalla linea continua del cursore.
    if (!app.dropTarget.empty() && app.dropTargetIsRow &&
        entry.wideName == LeafName(app.dropTarget))
        r.Draw(UIDraw::DashedBorder(card, t.accent, 2.0f * scale, 6.0f * scale, 4.0f * scale,
                                    6.0f * scale));

    ClickTarget target;
    target.rect  = card;
    target.kind  = ClickTarget::Kind::Row;
    target.index = view.entry;
    app.clickTargets.push_back(target);

    const float lineHeight = app.font.LineHeight() * scale;
    const float labelTop   = card.bottom() - lineHeight * 2.0f;
    const float iconArea   = (std::max)(16.0f, labelTop - card.y - 4.0f * scale);
    const float iconSize   = (std::min)(metrics.iconSize, iconArea);
    const Rect icon{card.x + (card.w - iconSize) * 0.5f,
                    card.y + (iconArea - iconSize) * 0.5f,
                    iconSize, iconSize};

    ShellPreviewCache::IconUV preview;
    // Fuori da una cartella normale un'anteprima non c'e': `Query` riceve il nome
    // di spazio dei nomi, se c'e', e risponde "niente" invece di leggere un
    // percorso che non esiste.
    const ShellPreviewCache::State state =
        app.previews.Query(EntryLocation(app, entry), entry.extension, &preview);

    if (state == ShellPreviewCache::State::Ready)
    {
        r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             preview.u0, preview.v0, preview.u1, preview.v1));
    }
    else if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor(entry.extension, entry.isDirectory))
    {
        r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                             uv->u0, uv->v0, uv->u1, uv->v1));
    }

    const std::vector<std::string> lines =
        WrapName(app.font, entry.name, scale, card.w - 8.0f * scale, 2);
    float textY = labelTop;
    float lastX = card.x;
    float lastWidth = 0.0f;
    for (const std::string& line : lines)
    {
        const float width = app.font.Measure(line, scale);
        lastX = card.x + (card.w - width) * 0.5f;
        lastWidth = width;
        app.font.Draw(r, lastX, textY, line, t.text, scale);
        textY += lineHeight;
    }

    // La sottolineatura va sotto l'ultima riga del nome, che e' l'ultima che si
    // legge: sotto la prima sarebbe sotto una riga di testo che continua.
    if (app.underlineOnHover && view.entry == app.hovered && !lines.empty())
        r.FillRect(Rect{lastX, textY - lineHeight + app.font.LineHeight() * scale - 1.0f * scale,
                        lastWidth, 1.0f * scale},
                   t.text);
}

void DrawList(AppState& app, const Layout& layout, const float* columnX)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const int cursorIndex = CursorIndex(app);
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const float rowHeight = metrics.rowHeight;

    r.Draw(UIDraw::Fill(layout.list, t.panelBg, 6.0f * scale));

    // Il fondo della lista riceve la caduta nella cartella aperta: si evidenzia
    // l'area, perche' li' sotto non c'e' nessuna riga da tratteggiare. Il
    // tratteggio e' lo stesso della riga, cosi' le due destinazioni si leggono
    // come la stessa cosa detta in due punti diversi.
    if (!app.dropTarget.empty() && !app.dropTargetIsRow && app.dropTargetSidebarRow < 0)
        r.Draw(UIDraw::DashedBorder(layout.list.Inset(1.0f, 1.0f), t.accent, 2.0f * scale,
                                    6.0f * scale, 4.0f * scale, 6.0f * scale));

    if (!app.errorText.empty())
    {
        app.font.Draw(r, layout.list.x + 16.0f * scale, layout.list.y + 16.0f * scale,
                      app.errorText, t.textDim, scale);
        return;
    }

    // A grid packs several rows of the view into one visual row, so the window of
    // interesting rows is computed in items, not in visual rows.
    int first = 0;
    int last  = static_cast<int>(app.rows.size());
    if (metrics.grid)
    {
        // Con le bande di gruppo non c'e' piu' una corrispondenza fra indice e
        // posizione, quindi la finestra non si calcola: si guarda la geometria, e
        // le righe fuori dal riquadro si saltano con lo stesso `continue` di
        // sempre. Non si disegnano e non lasciano bersagli.
        first = 0;
        last  = static_cast<int>(app.rows.size());
    }
    else
    {
        first = (std::max)(0, static_cast<int>(app.scroll / rowHeight) - 1);
        last  = (std::min)(last, first + static_cast<int>(layout.list.h / rowHeight) + 3);
    }

    for (int rowIndex = first; rowIndex < last; ++rowIndex)
    {
        const Row& view = app.rows[static_cast<size_t>(rowIndex)];
        const Rect row = ItemRect(app, layout, rowIndex, app.scroll);

        if (row.y < layout.list.y - row.h || row.y > layout.list.bottom())
            continue;

        // A group band is a row like any other: it scrolls, it is hit tested, and
        // clicking it folds the group away. Folding is remembered by label, so it
        // survives a rebuild of the view.
        if (view.header)
        {
            const Group& group = app.groups[static_cast<size_t>(view.group)];
            const bool collapsed = app.collapsedGroups.count(group.label) != 0;

            r.FillRect(row, t.headerBg);
            const std::string title = (collapsed ? "> " : "v ") + group.label;
            app.smallFont.Draw(r, row.x + 8.0f * scale, TextY(layout, row, app.smallFont),
                          title, t.text, scale);

            const std::string count = std::to_string(group.count) + " items";
            app.smallFont.Draw(r, row.right() - app.smallFont.Measure(count, scale) - 10.0f * scale,
                          TextY(layout, row, app.smallFont), count, t.textDim, scale);

            ClickTarget target;
            target.rect  = row;
            target.kind  = ClickTarget::Kind::GroupHeader;
            target.index = view.group;
            target.label = group.label;
            app.clickTargets.push_back(target);
            continue;
        }

        if (metrics.grid)
        {
            DrawEntryCell(app, layout, metrics, row, view, cursorIndex);
            continue;
        }

        const Entry& entry = app.entries[static_cast<size_t>(view.entry)];
        // L'ordine e' deliberato: selezione, poi sovrappasso, poi righe alternate.
        // Il sovrappasso deve stare PRIMA dell'alternanza, o sarebbe visibile
        // solo sulle righe pari e a chiazze.
        if (IsEntrySelected(app, entry))
            r.FillRect(row, t.rowSelected, 3.0f * scale);
        else if (view.entry == app.hovered)
            r.FillRect(row, t.rowHover, 3.0f * scale);
        else if (app.alternatingRows && rowIndex % 2 == 1)
            r.FillRect(row, t.rowOdd, 3.0f * scale);

        // The selection has a fill; the cursor has an outline. The reference theme
        // keeps them as separate tokens (ContentSelection, OutlineSelection) for
        // exactly this reason: with more than one row selected the user still has
        // to be able to see which one the keyboard is on.
        if (view.entry == cursorIndex)
            r.Draw(UIDraw::Border(row.Inset(1.0f, 1.0f), t.accent.WithAlpha(0.9f), 1.0f, 2.0f * scale));

        // La destinazione di una caduta in corso.
        if (!app.dropTarget.empty() && app.dropTargetIsRow &&
            entry.wideName == LeafName(app.dropTarget))
            r.Draw(UIDraw::DashedBorder(row.Inset(1.0f, 1.0f), t.accent, 2.0f * scale,
                                        6.0f * scale, 4.0f * scale, 6.0f * scale));

        ClickTarget target;
        target.rect = row;
        target.kind = ClickTarget::Kind::Row;
        target.index = view.entry;
        app.clickTargets.push_back(target);

        // Dopo la riga: `HitTest` cammina all'indietro, quindi la casella vince sul
        // clic che aprirebbe.
        if (app.showCheckboxes)
            DrawCheckbox(app, row, IsEntrySelected(app, entry), scale, 6.0f * scale, view.entry);

        // A real thumbnail replaces the type icon when one is ready. Asking is
        // free: an unavailable type never reaches the worker, and the request is
        // de-duplicated while a decode is in flight.
        const Rect icon{row.x + 6.0f * scale +
                            (app.showCheckboxes ? 22.0f * scale : 0.0f),
                        row.y + (rowHeight - metrics.iconSize) * 0.5f,
                        metrics.iconSize, metrics.iconSize};
        ShellPreviewCache::IconUV preview;
        const ShellPreviewCache::State previewState =
            app.previews.Query(EntryLocation(app, entry), entry.extension, &preview);

        if (previewState == ShellPreviewCache::State::Ready)
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                                 preview.u0, preview.v0, preview.u1, preview.v1));
        }
        else if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor(entry.extension, entry.isDirectory))
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        const float textY = TextY(layout, row, app.font);
        const std::wstring fullPath = EntryLocation(app, entry);

        // Numeric columns are right aligned so the digits line up down the
        // column, and underlined while the number is still growing.
        const auto columnRight = [&](int column) -> float
        {
            const float end = (column < kColumnCount - 1) ? columnX[column + 1]
                                                          : layout.header.right();
            return end - 8.0f * scale;
        };
        const auto drawRight = [&](int column, const std::string& text, Color color, bool growing)
        {
            const float right = columnRight(column);
            const float width = app.font.Measure(text, scale);
            app.font.Draw(r, right - width, textY, text, color, scale);
            if (growing)
                r.FillRect(Rect{right - width, row.bottom() - 5.0f * scale, width, 1.0f},
                           color.WithAlpha(0.6f));
        };

        // Con le caselle accese l'icona e il nome si spostano: la casella non
        // copre niente, si prende il suo posto.
        const float nameX   = row.x + 28.0f * scale +
                              (app.showCheckboxes ? 22.0f * scale : 0.0f);
        const float nameMax = columnX[1] - nameX - 12.0f * scale;

        if (app.renameActive && entry.wideName == app.renameOriginal)
        {
            // Il campo si disegna SOPRA la riga invece di sostituirne il
            // contenuto: la lista non cambia impaginazione mentre si scrive,
            // quindi niente si sposta sotto le dita di chi sta digitando.
            const Rect field{nameX - 3.0f * scale, row.y + 1.0f * scale,
                             nameMax + 6.0f * scale, row.h - 2.0f * scale};
            r.Draw(UIDraw::Fill(field, t.windowBg, 2.0f * scale));
            r.Draw(UIDraw::Border(field, t.accent, 1.0f, 2.0f * scale));

            const std::string shown =
                FitText(app.font, app.renameText, scale, field.w - 8.0f * scale);
            app.font.Draw(r, nameX + 1.0f * scale, textY, shown, t.text, scale);

            // Il cursore va dopo il testo VISIBILE, non dopo quello completo: se
            // il nome e' piu' lungo del campo, un cursore sul fondo del campo
            // sembrerebbe fermo mentre si scrive.
            const float caretX =
                nameX + 1.0f * scale + app.font.Measure(shown, scale) + 1.0f * scale;
            r.FillRect(Rect{caretX, row.y + 3.0f * scale, 1.0f * scale,
                            row.h - 6.0f * scale},
                       t.text);
        }
        else
        {
            const std::string shown = FitText(app.font, entry.name, scale, nameMax);
            app.font.Draw(r, nameX, textY, shown, t.text, scale);

            // `UnderlineOnHover`: la sottolineatura sta **sotto il nome**, e sotto
            // quello visibile quando il nome e' troncato — una riga lunga quanto
            // una parola che non c'e' sarebbe una bugia.
            if (app.underlineOnHover && view.entry == app.hovered)
                r.FillRect(Rect{nameX, textY + app.font.LineHeight() * scale - 1.0f * scale,
                                app.font.Measure(shown, scale), 1.0f * scale},
                           t.text);

            // Il bersaglio di un collegamento, dopo il nome e in tono spento: sta
            // nella stessa colonna, quindi prende lo spazio che resta — e se non ne
            // resta abbastanza non si scrive, invece di scrivere tre lettere e un
            // puntino.
            const std::wstring& linkTarget = ShortcutTarget(app, entry);
            if (!linkTarget.empty())
            {
                const float used = app.font.Measure(shown, scale);
                const float from = nameX + used + 14.0f * scale;
                const float room = columnX[1] - from - 8.0f * scale;
                if (room > 60.0f * scale)
                {
                    const std::string text = ToUtf8(linkTarget);
                    const float arrowWidth = app.font.Measure("> ", scale);
                    app.font.Draw(r, from, textY, "> ", t.textDim, scale);
                    app.font.Draw(r, from + arrowWidth, textY,
                                  FitText(app.font, text, scale, room - arrowWidth), t.textDim,
                                  scale);
                }
            }
        }

        const float typeMax = columnX[2] - columnX[1] - 12.0f * scale;
        app.font.Draw(r, columnX[1], textY, FitText(app.font, entry.typeText, scale, typeMax), t.textDim, scale);
        app.font.Draw(r, columnX[2], textY, entry.modifiedText, t.textDim, scale);

        // Files answer immediately; folders are answered by the statistics
        // service, which fills them in progressively. Requesting here means only
        // rows that are actually on screen get counted.
        // Sopra una cartella che non e' un percorso non c'e' niente da contare: le
        // statistiche ricorsive camminano il file system, e li' non ce n'e' uno.
        if (entry.isDirectory && entry.location.empty())
        {
            app.stats.Request(fullPath);

            DirStats stats;
            const bool known = app.stats.TryGet(fullPath, &stats);
            const Color numberColor = known ? t.text : t.textDim;
            const bool growing = known && stats.partial;

            drawRight(3, known ? HumanCount(stats.folders) : "--", numberColor, growing);
            drawRight(4, known ? HumanCount(stats.files) : "--", numberColor, growing);
            drawRight(5, known ? HumanBytes(stats.logicalBytes) : "--", numberColor, growing);
        }
        else
        {
            drawRight(3, "--", t.textDim, false);
            drawRight(4, "--", t.textDim, false);
            drawRight(5, entry.sizeText, t.textDim, false);
        }
    }

    // Scrollbar.
    const float contentHeight = ViewContentHeight(app, layout);
    if (contentHeight > layout.list.h)
    {
        const float trackHeight = layout.list.h - 8.0f;
        const float thumbHeight = (std::max)(24.0f * scale, trackHeight * (layout.list.h / contentHeight));
        const float maxScroll = (std::max)(1.0f, contentHeight - layout.list.h);
        const float thumbY = layout.list.y + 4.0f + (trackHeight - thumbHeight) * (app.scroll / maxScroll);

        const Rect track{layout.list.right() - 7.0f, layout.list.y + 4.0f, 4.0f, trackHeight};
        r.FillRect(track, t.separator, 2.0f * scale);
        r.FillRect(Rect{track.x, thumbY, track.w, thumbHeight}, t.scrollThumb, 2.0f * scale);
    }
}

// L'immagine grande dell'inspector: si decodifica quando cambia la voce scelta, e si
// carica nello strato una volta sola. La miniatura della shell arriva a 128 px e va
// bene per un'icona; qui il file si decodifica davvero, e ingrandirlo e' poi solo
// geometria di un quad.
//
// Si prova a decodificare solo per le estensioni che sono immagini: tentare su ogni
// file vorrebbe dire far aprire a WIC anche quello che non e' un'immagine, e su un
// JPEG da dieci megapixel la decodifica si sente.
bool LooksLikeImage(const std::string& extension)
{
    static const char* const kImageExtensions[] = {
        "jpg", "jpeg", "jpe", "png", "bmp", "gif", "tif", "tiff", "webp", "avif", "heic", "heif",
        "jfif", "ico",
    };
    for (const char* candidate : kImageExtensions)
    {
        if (extension == candidate)
            return true;
    }
    return false;
}

void EnsureBigImage(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    std::wstring path;
    if (cursorIndex >= 0)
    {
        const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
        if (!entry.isDirectory && entry.location.empty() && LooksLikeImage(entry.extension))
            path = JoinPath(app.directory, entry.wideName);
    }

    if (path == app.bigImagePath)
        return;

    app.bigImagePath     = path;
    app.bigImage         = path.empty() ? ImagePreview::Image{} : ImagePreview::Load(path, kBigImageSide);
    app.bigImageUploaded = false;
    app.imageZoom        = 1.0f;
}

// Disegna l'anteprima nell'area data, adattata e con l'ingrandimento scelto. Restituisce
// falso se non c'e' niente da disegnare, cosi' chi chiama sa che lo spazio resta al
// visualizzatore di testo o alle righe.
bool DrawBigImage(AppState& app, const Layout& layout, const Rect& area, float* usedHeight)
{
    if (!app.bigImage.Valid() || area.w < 40.0f || area.h < 40.0f)
        return false;

    if (!app.bigImageUploaded)
    {
        app.renderer.UploadLayerRegion(kBigImageLayer, app.bigImage.rgba.data(), app.bigImage.width,
                                       app.bigImage.height, 0, 0);
        app.bigImageUploaded = true;
    }

    // Adattata allo spazio, poi ingrandita: il fattore non scende sotto quello che fa
    // entrare l'immagine, cosi' "zoom indietro" non la fa uscire dal pannello.
    const float fit = (std::min)(area.w / static_cast<float>(app.bigImage.width),
                                 area.h / static_cast<float>(app.bigImage.height));
    const float scale = (std::max)(fit, fit * app.imageZoom);
    const float width  = static_cast<float>(app.bigImage.width) * scale;
    const float height = static_cast<float>(app.bigImage.height) * scale;

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const Rect frame{area.x, area.y, area.w, (std::min)(area.h, height + 8.0f * layout.scale)};
    r.Draw(UIDraw::Fill(frame, Color::FromHex(0x131417), 6.0f * layout.scale));

    // Il quad si disegna solo per la parte che entra: con l'ingrandimento acceso
    // l'immagine puo' essere piu' larga del pannello, e lasciarla uscire la farebbe
    // disegnare sopra il resto della finestra.
    const float visibleWidth  = (std::min)(width, area.w);
    const float visibleHeight = (std::min)(height, frame.h - 8.0f * layout.scale);
    const Rect picture{area.x + (area.w - visibleWidth) * 0.5f,
                       frame.y + (frame.h - visibleHeight) * 0.5f, visibleWidth, visibleHeight};
    const float u = (app.bigImage.width > 0)
                        ? (visibleWidth / width) * static_cast<float>(app.bigImage.width) /
                              static_cast<float>(kBigImageSide)
                        : 0.0f;
    const float v = (app.bigImage.height > 0)
                        ? (visibleHeight / height) * static_cast<float>(app.bigImage.height) /
                              static_cast<float>(kBigImageSide)
                        : 0.0f;
    r.Draw(UIDraw::Image(picture, Color{1.0f, 1.0f, 1.0f, 1.0f}, static_cast<float>(kBigImageLayer),
                         0.0f, 0.0f, u, v));
    r.Draw(UIDraw::Border(frame, t.panelBorder, 1.0f, 6.0f * layout.scale));

    if (usedHeight != nullptr)
        *usedHeight = frame.h + 10.0f * layout.scale;
    return true;
}

// --- visualizzatore di testo (M8) -------------------------------------------
//
// Tre passi, e ognuno ha una ragione: il **documento** si legge quando la voce
// scelta cambia (e' l'unica cosa in memoria che riguarda il contenuto di un file, e
// vive quanto la selezione: `docs/06`); le **righe come si vedono** si calcolano una
// volta per misura, perche' con l'andare a capo acceso una riga logica puo' essere
// piu' righe di schermo e rifarlo a ogni fotogramma sarebbe lavoro sprecato; il
// **disegno** legge solo quello che sta dentro la finestra.

void EnsureTextDocument(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    std::wstring path;
    if (cursorIndex >= 0)
    {
        const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
        // Solo per i file che stanno su un percorso: un nome di spazio dei nomi non
        // si apre come testo.
        if (!entry.isDirectory && entry.location.empty())
            path = JoinPath(app.directory, entry.wideName);
    }

    if (path == app.textDocumentPath)
        return;   // gia' letto, o gia' saputo che non c'e' niente da leggere

    app.textDocumentPath = path;
    app.textDocument     = path.empty() ? TextView::Document{} : TextView::Load(path);
    app.textScroll       = 0.0f;
    app.textRowsDirty    = true;
}

void LayoutTextRows(AppState& app, const FontAtlas& font, float width, float textScale)
{
    if (!app.textRowsDirty && app.textRowsWidth == width && app.textRowsZoom == textScale)
        return;

    app.textRows.clear();
    app.textRowsWidth = width;
    app.textRowsZoom  = textScale;
    app.textRowsDirty = false;
    app.textLongest   = 0;

    const std::vector<std::string>& lines = app.textDocument.lines;
    for (size_t index = 0; index < lines.size(); ++index)
    {
        const std::string& line = lines[index];
        app.textLongest = (std::max)(app.textLongest, line.size());

        if (!app.textWrap || line.empty())
        {
            app.textRows.push_back(TextRow{index, 0, line.size()});
            continue;
        }

        // Andare a capo: si cammina la riga e si taglia dove non ci sta piu'. Il
        // taglio cade fra due caratteri, come tutti i tagli di testo di questo
        // programma, e va avanti anche quando una parola da sola non ci sta.
        size_t start = 0;
        float used = 0.0f;
        for (size_t i = 0; i < line.size(); i = Utf8::Next(line, i))
        {
            const float advance = font.MeasureOne(Utf8::At(line, i, nullptr), textScale);
            if (used + advance > width && i > start)
            {
                app.textRows.push_back(TextRow{index, start, i - start});
                start = i;
                used = 0.0f;
            }
            used += advance;
        }
        app.textRows.push_back(TextRow{index, start, line.size() - start});
    }
}

// Un carattere come stringa UTF-8: `FontAtlas::Draw` vuole una stringa, e costruirla
// con `substr` per ogni lettera di ogni riga visibile sarebbe un'allocazione per
// lettera. Le stringhe corte non toccano l'heap, quindi questa non costa niente.
std::string OneChar(uint32_t codepoint)
{
    std::string text;
    if (codepoint < 0x80)
    {
        text.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800)
    {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint < 0x10000)
    {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return text;
}

// Una riga di testo, con gli spazi visibili se accesi: un punto dove c'e' uno spazio,
// una freccia dove c'e' una tabulazione. E' l'unico modo di vedere *cosa* c'e' dove
// sembra non esserci niente, ed e' quello per cui esiste `InspectorShowWhitespace`.
void DrawTextRow(UIRenderer& r, const FontAtlas& font, const std::string& line, const TextRow& row,
                 float x, float y, float scale, bool whitespace, Color color, float maxWidth)
{
    const float space = font.MeasureOne(' ', scale);
    const float limit = x + maxWidth;

    float penX = x;
    int column = 0;
    for (size_t i = row.offset; i < row.offset + row.length; i = Utf8::Next(line, i))
    {
        const uint32_t codepoint = Utf8::At(line, i, nullptr);

        if (codepoint == '\t')
        {
            // La tabulazione porta alla colonna successiva multipla di quattro: si
            // calcola quanto manca, invece di disegnarne sempre quattro.
            const int stop = (column / 4 + 1) * 4;
            if (whitespace && penX + space <= limit)
                font.Draw(r, penX, y, "\u2192", Color::FromHex(0x555B66), scale);
            penX = x + space * static_cast<float>(stop);
            column = stop;
            if (penX > limit)
                break;
            continue;
        }

        const float advance = font.MeasureOne(codepoint, scale);
        if (penX + advance > limit)
            break;   // fuori dal pannello: si smette, invece di disegnare sopra altro

        if (codepoint == ' ' && whitespace)
            font.Draw(r, penX, y, "\u00B7", Color::FromHex(0x555B66), scale);
        else
            font.Draw(r, penX, y, OneChar(codepoint), color, scale);

        penX += advance;
        ++column;
    }
}

void DrawTextViewer(AppState& app, const Layout& layout, const Rect& area)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const FontAtlas& font = (app.monoFontOk ? app.monoFont : app.smallFont);
    const TextView::Document& document = app.textDocument;

    // La riga di stato: la codifica, quante righe, con che terminatori. Sono le tre
    // cose che si vogliono sapere di un file di testo prima di leggerlo, e la codifica
    // e' quella che l'originale chiama `PanelInspector_TextEncodingUTF8`.
    char header[160];
    std::snprintf(header, sizeof(header), "%s   %zu lines%s%s",
                  TextView::EncodingName(document.encoding), document.lines.size(),
                  document.endings.empty() ? "" : "   ", document.endings.c_str());
    app.smallFont.Draw(r, area.x, area.y, header, t.textDim, scale);
    if (document.truncated)
        app.smallFont.Draw(r, area.x + app.smallFont.Measure(header, scale) + 12.0f * scale, area.y,
                           "(first 1 MB)", t.textDim, scale);

    const float top = area.y + 18.0f * scale;
    const Rect body{area.x, top, area.w, area.bottom() - top};
    if (body.h < 40.0f * scale || document.lines.empty())
        return;

    const float gutter = app.textLineNumbers ? 40.0f * scale : 0.0f;
    const float minimapWidth = app.textMinimap ? (std::min)(58.0f * scale, body.w * 0.25f) : 0.0f;
    const Rect text{body.x + gutter, body.y, body.w - gutter - minimapWidth, body.h};

    const float textScale = scale * app.textZoom;
    LayoutTextRows(app, font, text.w, textScale);

    const float lineHeight = font.LineHeight() * textScale;
    const float content = static_cast<float>(app.textRows.size()) * lineHeight;
    // Il limite e' **due**: sotto zero non si scorre, e oltre `content - text.h`
    // nemmeno. Prima ce n'era uno solo scritto al contrario — `min(max(0, content -
    // text.h), scroll)` limita solo in alto, e il `max(0, ...)` era applicato al
    // limite invece che allo scorrimento. Scorrendo la rotella verso l'alto oltre
    // l'inizio, `textScroll` diventava negativo e li' restava: il testo veniva
    // spinto verso il basso fino a sparire, e la fascia della minimap usciva dal
    // suo riquadro e finiva sopra il pannello. Trovato dall'autore, guardando.
    app.textScroll = (std::max)(0.0f, (std::min)(app.textScroll, content - text.h));

    const int first = (std::max)(0, static_cast<int>(app.textScroll / lineHeight) - 1);
    const int last = (std::min)(static_cast<int>(app.textRows.size()),
                                first + static_cast<int>(text.h / lineHeight) + 3);

    // Le righe si ritagliano sull'area del testo, come la lista sul suo rettangolo
    // (§7.36). Il `first` qui sopra comincia **una riga prima** di quella visibile,
    // cosi' la riga a meta' si vede a meta' invece di comparire di colpo — ed e'
    // proprio quella riga che, senza ritaglio, finisce sopra la testata
    // `UTF-8 ... righe`. Si vedeva scorrendo.
    // Il ritaglio parte dal bordo del pannello, non da quello del testo: i numeri di
    // riga stanno nella gutter, a sinistra, e ritagliando sul solo testo sparivano —
    // un ritaglio stretto e' un difetto quanto uno largo.
    app.renderer.End();
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(body.x),
              static_cast<GLint>(static_cast<float>(app.viewportHeight) - text.bottom()),
              static_cast<GLsizei>(text.right() - body.x), static_cast<GLsizei>(text.h));
    app.renderer.Begin(app.viewportWidth, app.viewportHeight);

    for (int row = first; row < last; ++row)
    {
        const TextRow& item = app.textRows[static_cast<size_t>(row)];
        const float y = text.y + static_cast<float>(row) * lineHeight - app.textScroll;

        // Il numero si scrive solo dove comincia una riga *logica*: quando una riga va
        // a capo, le sue continuazioni appartengono alla stessa riga.
        if (app.textLineNumbers && item.offset == 0)
        {
            const std::string number = std::to_string(item.line + 1);
            app.smallFont.Draw(r, body.x + gutter - app.smallFont.Measure(number, scale) - 10.0f * scale,
                               y, number, t.textDim, scale);
        }

        DrawTextRow(r, font, document.lines[item.line], item, text.x, y, textScale,
                    app.textShowWhitespace, t.text, text.w);
    }

    app.renderer.End();
    glDisable(GL_SCISSOR_TEST);
    app.renderer.Begin(app.viewportWidth, app.viewportHeight);

    // La minimap: una barra per riga di schermo, lunga quanto la riga piu' lunga che
    // ci sta dentro, e la parte che si sta guardando evidenziata. Serve a orientarsi in
    // un file lungo, che e' l'unica cosa che chiede di essere guardata cosi'.
    if (app.textMinimap && minimapWidth > 0.0f && app.textLongest > 0)
    {
        const Rect map{text.right() + 10.0f * scale, body.y, minimapWidth - 10.0f * scale, body.h};
        const int bands = (std::max)(1, static_cast<int>(map.h));
        const double rows = static_cast<double>(app.textRows.size());
        for (int band = 0; band < bands; ++band)
        {
            const size_t from = static_cast<size_t>(static_cast<double>(band) / bands * rows);
            const size_t to = (std::max)(from + 1, static_cast<size_t>(
                                                       static_cast<double>(band + 1) / bands * rows));
            size_t longest = 0;
            for (size_t i = from; i < to && i < app.textRows.size(); ++i)
                longest = (std::max)(longest, app.textRows[i].length);
            if (longest == 0)
                continue;
            const float fraction =
                (std::min)(1.0f, static_cast<float>(longest) / static_cast<float>(app.textLongest));
            r.FillRect(Rect{map.x, map.y + static_cast<float>(band), map.w * fraction, 1.0f},
                       t.textDim.WithAlpha(0.45f));
        }

        if (content > text.h && content > 0.0f)
        {
            const float visibleTop = app.textScroll / content * map.h;
            const float visibleHeight = (std::min)(map.h, text.h / content * map.h);
            r.FillRect(Rect{map.x, map.y + visibleTop, map.w, visibleHeight},
                       t.accent.WithAlpha(0.18f));
        }
    }
}

void DrawInspector(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.Draw(UIDraw::Shadow(Rect{layout.inspector.x + 2.0f, layout.inspector.y + 3.0f,
                               layout.inspector.w, layout.inspector.h},
                          Color{0.0f, 0.0f, 0.0f, 0.32f}, 10.0f * scale, 8.0f * scale));
    r.Draw(UIDraw::Fill(layout.inspector, t.panelBg, 8.0f * scale));
    r.Draw(UIDraw::Border(layout.inspector, t.panelBorder, 1.0f, 8.0f * scale));

    float y = layout.inspector.y + 13.0f * scale;
    app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y, "INSPECTOR", t.textDim, scale);
    y += 24.0f * scale;

    // Il documento del visualizzatore, se ce n'e' uno da leggere: la lettura avviene
    // qui e non a ogni fotogramma, perche' cambia solo quando cambia la voce scelta.
    EnsureTextDocument(app);

    const int selectedCount = static_cast<int>(app.selection.size());
    const int cursorIndex = CursorIndex(app);

    if (selectedCount == 0 || cursorIndex < 0)
    {
        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y, Tr("Niente selezionato", "Nothing selected"), t.textDim, scale);
        return;
    }

    if (selectedCount > 1)
    {
        // The reference inspector has labels for "file singolo o conteggio" and
        // "dimensione o somma": with several entries selected it reports the
        // aggregate. Folder sizes come from the cache only, because asking for a
        // folder that is not on screen would start work the user cannot see.
        uint64_t totalBytes = 0;
        int files = 0;
        int folders = 0;
        for (const std::wstring& name : app.selection)
        {
            const int index = FindEntryByName(app, name);
            if (index < 0)
                continue;
            const Entry& item = app.entries[static_cast<size_t>(index)];
            if (item.isDirectory)
            {
                ++folders;
                DirStats stats;
                if (app.stats.TryGet(JoinPath(app.directory, item.wideName), &stats))
                    totalBytes += stats.logicalBytes;
            }
            else
            {
                ++files;
                totalBytes += item.bytes;
            }
        }

        const std::string title = std::to_string(selectedCount) + Tr(" elementi selezionati", " items selected");
        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y, title, t.text, scale);
        y += 26.0f * scale;

        const std::string breakdown =
            std::to_string(files) + Tr(" file, ", " files, ") + std::to_string(folders) +
            Tr(" cartelle", " folders");
        // Che tipi sono: "3 Documento di testo, 2 Cartella di file". Il conteggio
        // per tipo e' quello che dice cosa si sta per spostare o cancellare, e
        // l'inspector e' l'unico posto dove c'e' spazio per dirlo.
        std::unordered_map<std::string, int> byType;
        for (const std::wstring& name : app.selection)
        {
            const int index = FindEntryByName(app, name);
            if (index >= 0)
                ++byType[app.entries[static_cast<size_t>(index)].typeText];
        }
        std::vector<std::pair<std::string, int>> types(byType.begin(), byType.end());
        std::sort(types.begin(), types.end(),
                  [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                      return a.second > b.second;
                  });
        const std::pair<const char*, std::string> summary[] = {
            {"Items", breakdown},
            {"Size", HumanBytes(totalBytes)},
        };
        for (const auto& item : summary)
        {
            app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y, item.first, t.textDim, scale);
            app.smallFont.Draw(r, layout.inspector.x + 96.0f * scale, y, item.second, t.text, scale);
            y += 20.0f * scale;
        }

        // Un tipo per riga invece di tutti su una: "3 Documento di testo, 2 Cartella
        // di file" non ci sta in una riga sola, e troncato dice la meta' di quello che
        // serve. L'etichetta sta sulla prima, le altre si allineano sotto.
        for (size_t i = 0; i < types.size() && i < 4; ++i)
        {
            app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y,
                               (i == 0) ? "Types" : "", t.textDim, scale);
            const float valueX = layout.inspector.x + 96.0f * scale;
            const float valueMax = layout.inspector.right() - valueX - 14.0f * scale;
            const std::string line = std::to_string(types[i].second) + " " + types[i].first;
            app.smallFont.Draw(r, valueX, y, FitText(app.smallFont, line, scale, valueMax), t.text,
                               scale);
            y += 20.0f * scale;
        }
        if (types.size() > 4)
        {
            app.smallFont.Draw(r, layout.inspector.x + 96.0f * scale, y,
                               Tr("e altri ", "and ") + std::to_string(types.size() - 4) +
                                   Tr("", " more"),
                               t.textDim, scale);
        }
        return;
    }

    const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];

    ShellPreviewCache::IconUV preview;
    const ShellPreviewCache::State previewState =
        app.previews.Query(EntryLocation(app, entry), entry.extension, &preview);

    // L'anteprima grande: il file decodificato davvero, adattato al pannello e
    // ingrandibile. Prende il posto della miniatura da 128 px, che per una fotografia
    // e' un'icona piu' grande, non un'anteprima.
    EnsureBigImage(app);
    const Rect previewArea{layout.inspector.x + 14.0f * scale, y,
                           layout.inspector.w - 28.0f * scale,
                           (std::min)(260.0f * scale, layout.inspector.h * 0.42f)};
    float imageHeight = 0.0f;
    const bool hasBigImage = DrawBigImage(app, layout, previewArea, &imageHeight);
    if (hasBigImage)
        y += imageHeight;

    // Per un file di testo la miniatura della shell e' un'icona di documento: lo
    // spazio serve al testo, che e' l'anteprima vera.
    if (!hasBigImage && previewState == ShellPreviewCache::State::Ready && !app.textDocument.isText)
    {
        // A ready thumbnail gets the width of the panel and pushes the name
        // underneath it: this is the one place large enough to be worth looking at.
        const float side = 128.0f * scale;
        const Rect frame{layout.inspector.x + 14.0f * scale, y, side, side};
        r.Draw(UIDraw::Fill(frame, Color::FromHex(0x131417), 6.0f * scale));
        r.Draw(UIDraw::Image(frame.Inset(1.0f, 1.0f), Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             preview.u0, preview.v0, preview.u1, preview.v1));
        r.Draw(UIDraw::Border(frame, t.panelBorder, 1.0f, 6.0f * scale));
        y += side + 12.0f * scale;

        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y,
                      FitText(app.font, entry.name, scale, layout.inspector.w - 28.0f * scale),
                      t.text, scale);
        y += 26.0f * scale;
    }
    else
    {
        if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor(entry.extension, entry.isDirectory))
        {
            const Rect icon{layout.inspector.x + 14.0f * scale, y, 32.0f * scale, 32.0f * scale};
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        // Name too long for the inspector column is truncated the same way.
        const float valueMax = layout.inspector.w - 110.0f * scale;
        app.font.Draw(r, layout.inspector.x + 56.0f * scale, y + 8.0f * scale,
                      FitText(app.font, entry.name, scale, valueMax), t.text, scale);
        y += 46.0f * scale;
    }

    // Un **volume** non e' una cartella: le sue dimensioni sono la capacita' del
    // disco, non la somma di quello che c'e' dentro, e le statistiche ricorsive li'
    // non hanno niente da camminare. Prima si chiedeva lo stesso il conto, e per un
    // disco l'inspector rispondeva "0 B, 0 cartelle, 0 file": un numero falso detto
    // con sicurezza, che e' peggio di nessun numero.
    const std::wstring full = EntryLocation(app, entry);
    const VolumeInfo volume = VolumeOf(full);
    const bool fromNamespace = !entry.location.empty();

    // Folder numbers, if the panel is counting them. Asking here covers the
    // selected row even when it has scrolled out of view.
    DirStats stats;
    bool hasStats = false;
    if (entry.isDirectory && !volume.isVolume && !fromNamespace)
    {
        app.stats.Request(full);
        hasStats = app.stats.TryGet(full, &stats);
    }

    // L'etichetta non e' per forza una costante: le colonne della shell hanno i nomi
    // che sceglie lei, e sono tradotti.
    struct Row { std::string label; std::string value; bool growing; bool tail = false; };
    std::vector<Row> rows;

    // Una voce di una cartella di spazio dei nomi — un file riciclato — la descrive
    // **la shell**, con le sue colonne: tipo, dimensione, date, percorso originale,
    // data di eliminazione, attributi, proprietario. Sono le stesse informazioni che
    // darebbero le righe qui sotto, piu' quelle che solo il cestino sa. Mostrarle
    // tutte e due riempirebbe il pannello di righe ripetute, quindi per queste voci
    // si usa la vista della shell e basta.
    const bool shellColumns = !entry.pidl.empty();
    if (shellColumns)
    {
        for (const ShellLocation::Column& column : ShellLocation::Columns(entry.pidl))
        {
            if (rows.size() >= 11)
                break;   // oltre, le righe uscirebbero dal pannello
            rows.push_back({ToUtf8(column.title), ToUtf8(column.value), false});
        }
    }

    if (!shellColumns)
        rows.push_back({"Type", entry.typeText, false});
    if (volume.isVolume)
    {
        if (!volume.totalText.empty())
            rows.push_back({"Size", volume.totalText, false});
    }
    else if (!shellColumns && entry.isDirectory)
    {
        const bool growing = hasStats && stats.partial;
        rows.push_back({"Size",    hasStats ? HumanBytes(stats.logicalBytes)
                                               : Tr("conteggio...", "counting..."), growing});
        rows.push_back({"Folders", hasStats ? HumanCount(stats.folders) : "--", growing});
        rows.push_back({"Files",   hasStats ? HumanCount(stats.files) : "--", growing});
    }
    else if (!shellColumns)
    {
        rows.push_back({"Size", entry.sizeText, false});
    }
    // Le date che il file system da' e che non si vedevano: la creazione e l'ultimo
    // accesso. Un tempo a zero non e' il 1601, e' "non lo so": la riga non si mette,
    // e vale anche per la modifica — un volume non ne ha una, e una riga con
    // l'etichetta e niente accanto non dice niente.
    const auto dated = [](const FILETIME& time) {
        return time.dwLowDateTime != 0 || time.dwHighDateTime != 0;
    };
    if (!shellColumns)
    {
        if (dated(entry.created))
            rows.push_back({"Created", FormatFileTime(entry.created, /*withTime=*/true), false});
        if (dated(entry.modified))
            rows.push_back({"Modified", entry.modifiedFull, false});
        if (dated(entry.accessed))
            rows.push_back({"Accessed", FormatFileTime(entry.accessed, /*withTime=*/true), false});
    }

    // Le proprieta' che il file system non dice: le dimensioni di un'immagine le sa
    // il gestore di quel tipo, e le risponde la shell.
    const FileProperties& properties = PropertiesOf(app, full);
    if (properties.imageWidth > 0 && properties.imageHeight > 0)
    {
        rows.push_back({"Image", std::to_string(properties.imageWidth) + " x " +
                                     std::to_string(properties.imageHeight) + " px",
                        false});
    }

    // Un volume: il file system e lo spazio. Sono proprieta' del volume, non del
    // file che ci sta sopra, e si chiedono al sistema.
    if (volume.isVolume)
    {
        if (!volume.fileSystem.empty())
            rows.push_back({"System", volume.fileSystem, false});
        if (!volume.freeText.empty())
        {
            char free[96];
            std::snprintf(free, sizeof(free), "%s free (%d%% used)", volume.freeText.c_str(),
                          static_cast<int>(volume.usedFraction * 100.0f));
            rows.push_back({"Space", free, false});
        }
    }

    // Il percorso: e' la riga che si copia, e l'unica che dice *dove* si sta
    // guardando. Si taglia davanti.
    rows.push_back({"Path", ToUtf8(full), false, /*tail=*/true});

    // "Ext" solo per quello che un'estensione ce l'ha: una riga con l'etichetta e
    // niente accanto occupa lo spazio di una che direbbe qualcosa. E il trattino
    // lungo che c'era prima non era nemmeno un carattere che l'atlante conosceva:
    // a schermo si vedeva un buco, non un trattino.
    if (!entry.extension.empty())
        rows.push_back({"Ext", entry.extension, false});

    for (const Row& row : rows)
    {
        // L'etichetta si tronca alla colonna che ha: una colonna della shell puo'
        // chiamarsi "Posizione originale" e finire sopra il valore.
        const float labelX = layout.inspector.x + 14.0f * scale;
        app.smallFont.Draw(r, labelX, y,
                           FitText(app.smallFont, row.label, scale, 74.0f * scale), t.textDim,
                           scale);
        const float valueX = layout.inspector.x + 96.0f * scale;
        const float valueMax = layout.inspector.right() - valueX - 14.0f * scale;
        // Un percorso si taglia davanti, tutto il resto dietro: le due cose che si
        // vogliono sapere sono "dove finisce" e "cosa c'e' scritto".
        const std::string shown = row.tail
                                      ? FitTextTail(app.smallFont, row.value, scale, valueMax)
                                      : FitText(app.smallFont, row.value, scale, valueMax);
        app.smallFont.Draw(r, valueX, y, shown, t.text, scale);
        if (row.growing)
        {
            const float width = app.smallFont.Measure(row.value, scale);
            r.FillRect(Rect{valueX, y + 15.0f * scale, width, 1.0f}, t.textDim);
        }
        y += 20.0f * scale;
    }

    // Il testo del file, sotto i metadati: i metadati restano la prima cosa da
    // leggere, e il testo prende lo spazio che avanza. Se non ne avanza abbastanza non
    // si mostra niente, invece di mostrare tre righe in un buco.
    app.textViewerRect = Rect{};
    if (app.textDocument.isText)
    {
        const Rect remaining{layout.inspector.x + 14.0f * scale, y,
                             layout.inspector.w - 28.0f * scale,
                             layout.inspector.bottom() - y - 12.0f * scale};
        if (remaining.h > 70.0f * scale)
        {
            app.textViewerRect = remaining;
            DrawTextViewer(app, layout, remaining);
        }
    }

    // Qui c'era un riquadro tratteggiato "Drop files here (not wired up yet)",
    // lasciato da M1 quando il tratteggio serviva solo a dare qualcosa da fare al
    // tracciato dello shader. Adesso il tratteggio ha il suo uso vero —
    // l'evidenziazione della destinazione di una caduta, in `DrawList` — e questo
    // segnaposto prometteva qualcosa che nessuna milestone ha mai previsto.
}

void DrawStatusBar(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    // Un esito recente prende il posto di tutto il resto. Chi ha appena premuto un
    // tasto guarda li' per sapere com'e' andata, e i numeri non rispondono alla sua
    // domanda.
    const bool showingResult = !app.statusMessage.empty() &&
                               NowSeconds() - app.statusMessageTime < 4.0;
    if (showingResult)
    {
        app.smallFont.Draw(r, 12.0f * scale, layout.statusBar.y + 4.0f * scale,
                           ToUtf8(app.statusMessage), t.accent, scale);
        return;
    }

    // A sinistra quante cose ci sono e quante sono scelte; a destra dove si e' nella
    // lista, come si sta guardando, e quanto e' ingrandita. Erano contatori del
    // motore — fotogrammi, quadrilateri, lavori in corso — che servono a chi scrive
    // il programma e non a chi lo usa: adesso stanno dietro `BOLT_DEV_HUD=1`.
    char counts[96];
    std::snprintf(counts, sizeof(counts), "%zu items", app.entries.size());
    app.smallFont.Draw(r, 12.0f * scale, layout.statusBar.y + 4.0f * scale, counts, t.textDim, scale);

    if (!app.selection.empty())
    {
        char chosen[96];
        std::snprintf(chosen, sizeof(chosen), "%zu selected", app.selection.size());
        const float x = 12.0f * scale + app.smallFont.Measure(counts, scale) + 16.0f * scale;
        app.smallFont.Draw(r, x, layout.statusBar.y + 4.0f * scale, chosen, t.text, scale);
    }

    if (DevHudEnabled())
    {
        char dev[192];
        std::snprintf(dev, sizeof(dev), "%.0f fps | %.0f quads | %d icons, %d previews | %d jobs",
                      static_cast<double>(app.framesPerSecond),
                      static_cast<double>(app.renderer.QuadCount()), app.icons.IconCount(),
                      app.previews.ReadyCount(), app.stats.ActiveJobs());
        const float x = 12.0f * scale + app.font.Measure("0000 items", scale) +
                        app.smallFont.Measure("000 selected", scale) + 40.0f * scale;
        app.smallFont.Draw(r, x, layout.statusBar.y + 4.0f * scale, dev, t.textDim, scale);
    }

    // A destra, al contrario: la posizione nella lista. Il conto e' fra le voci
    // **visibili** — i gruppi aggiungono righe che non sono voci, e un filtro ne
    // toglie — ed e' quello che serve a orientarsi.
    int visible = 0;
    int position = 0;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        ++visible;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (position == 0 && entry.wideName == app.cursor)
            position = visible;
    }

    std::string right;
    if (position > 0)
        right = std::to_string(position) + " / " + std::to_string(visible);

    const char* view = (app.viewMode == ViewMode::Details) ? "Details"
                     : (app.viewMode == ViewMode::Tiles)   ? "Tiles"
                                                           : "Icons";
    if (!right.empty())
        right += "   |   ";
    right += view;
    right += "   |   ";
    right += std::to_string(static_cast<int>(app.zoom * 100.0f / (app.dpiScale > 0.0f ? app.dpiScale : 1.0f)));
    right += "%";

    const float rightWidth = app.smallFont.Measure(right, scale);
    app.smallFont.Draw(r, layout.statusBar.right() - rightWidth - 14.0f * scale,
                       layout.statusBar.y + 4.0f * scale, right, t.textDim, scale);
}

// --- command palette: registry ----------------------------------------------
struct CommandDef
{
    const char* group;
    const char* label;
    CommandId   id;
};

// Group names come from the reference key table (the 19 palette groups). Only the
// groups this shell can carry out are present.
const CommandDef kCommandTable[] = {
    // La lingua: due voci, non un interruttore. Con due si vede sempre quale e'
    // accesa, e non serve indovinare lo stato di partenza.
    {"Language", "Language: Italian",           CmdLanguageItalian},
    {"Language", "Language: English",           CmdLanguageEnglish},
    {"Language", "Where the settings file is",  CmdShowSettingsPath},
    {"Tabs",    "New tab",                     CmdNewTab},
    {"Tabs",    "Close tab and its children",   CmdCloseInTree},
    {"Tabs",    "Close the whole tab tree",     CmdCloseAllInTree},
    {"Tabs",    "Detach tab from its parent",   CmdDetachTab},
    {"Tabs",    "Close current tab",           CmdCloseTab},
    {"Tabs",    "Next tab",                    CmdNextTab},
    {"Tabs",    "Previous tab",                CmdPrevTab},
    {"Folder",  "Go to parent folder",         CmdGoUp},
    {"Folder",  "Go back",                     CmdBack},
    {"Folder",  "Go forward",                  CmdForward},
    {"Folder",  "Refresh",                     CmdRefresh},
    {"Folder",  "Recalculate folder sizes",    CmdRecalcStats},
    {"Folder",  "Open in Explorer",            CmdOpenExplorer},
    {"Folder",  "Open a console here",         CmdOpenConsole},
    {"File",    "Copy",                        CmdCopy},
    {"File",    "Cut",                         CmdCut},
    {"File",    "Paste",                       CmdPaste},
    {"File",    "Rename",                      CmdRename},
    {"File",    "New folder",                  CmdNewFolder},
    {"File",    "Delete (recycle bin)",        CmdDelete},
    {"File",    "Delete permanently",          CmdDeletePermanent},
    {"GoTo",    "Go to folder...",             CmdGoTo},
    {"View",    "View: details",               CmdViewDetails},
    {"View",    "View: list",                  CmdViewList},
    {"View",    "View: tiles",                 CmdViewTiles},
    {"View",    "View: icons",                 CmdViewIcons},
    {"View",    "Toggle sidebar",              CmdToggleSidebar},
    {"View",    "Hover to select",              CmdToggleHoverToSelect},
    {"View",    "Underline on hover",           CmdToggleUnderlineOnHover},
    {"View",    "Show tooltips",                CmdToggleTooltips},
    // "Interaction" e' il nome del gruppo nella tabella delle chiavi. Sono cinque
    // chiavi, non cinque funzioni: nessuna fa niente da sola, cambiano *come* si
    // apre e si sceglie.
    {"Interaction", "Single click opens",         CmdSingleClick},
    {"Interaction", "Single click: folders only", CmdSingleClickFoldersOnly},
    {"Interaction", "Act on press",               CmdActOnPress},
    {"Interaction", "Double click on empty space goes up", CmdDoubleClickToParent},
    {"Interaction", "Show checkboxes",            CmdShowCheckboxes},
    {"View",    "Zoom in",                     CmdZoomIn},
    // Le opzioni del visualizzatore di testo. Nella tabella delle chiavi
    // dell'originale sono `InspectorShowWhitespace`, `InspectorHideLineNumbers`,
    // `InspectorHideMinimap`, `InspectorWordWrap`, `InspectorZoomLevel`: le stesse
    // cose, con i nomi che ha scelto chi le ha scritte la' dentro.
    {"Inspector", "Text: show whitespace",       CmdTextWhitespace},
    {"Inspector", "Text: toggle line numbers",   CmdTextLineNumbers},
    {"Inspector", "Text: toggle minimap",        CmdTextMinimap},
    {"Inspector", "Text: toggle word wrap",      CmdTextWrap},
    {"Inspector", "Text: zoom in",               CmdTextZoomIn},
    {"Inspector", "Text: zoom out",              CmdTextZoomOut},
    {"Inspector", "Image: zoom in",              CmdImageZoomIn},
    {"Inspector", "Image: zoom out",             CmdImageZoomOut},
    {"Inspector", "Image: fit to panel",         CmdImageZoomFit},
    {"View",    "Zoom out",                    CmdZoomOut},
    {"View",    "Reset zoom",                  CmdZoomReset},
    {"View",    "Toggle command details",      CmdToggleDetails},
    {"Sort",    "Sort by name",                CmdSortName},
    {"Sort",    "Sort by type",                CmdSortType},
    {"Sort",    "Sort by date modified",       CmdSortModified},
    {"Sort",    "Sort by folder count",        CmdSortFolders},
    {"Sort",    "Sort by file count",          CmdSortFiles},
    {"Sort",    "Sort by size",                CmdSortSize},
    {"Sort",    "Reverse sort direction",      CmdSortDirection},
    {"GroupBy", "Group: none",                 CmdGroupNone},
    {"GroupBy", "Group by name",               CmdGroupName},
    {"GroupBy", "Group by date",               CmdGroupDate},
    {"GroupBy", "Group by size",               CmdGroupSize},
    {"GroupBy", "Group by item count",         CmdGroupCount},
    {"GroupBy", "Group by volume usage",       CmdGroupVolume},
    {"Text",    "Focus the filter box",        CmdFocusFilter},
    {"Text",    "Clear the filter",            CmdClearFilter},
    {"Scroll",  "Scroll to top",               CmdScrollTop},
    {"Scroll",  "Scroll to bottom",            CmdScrollBottom},
    {"Bookmarks", "Bookmark this folder",      CmdToggleBookmark},
    {"Bookmarks", "Clear all bookmarks",       CmdClearBookmarks},
    {"Recents", "Clear recent locations",      CmdClearRecents},
    {"Options", "Open options",                 CmdOpenOptions},
};

bool MenuMatch(const std::string& text, const std::wstring& filter)
{
    if (filter.empty())
        return true;

    std::string lower = text;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    size_t start = 0;
    for (;;)
    {
        size_t space = filter.find(L' ', start);
        if (space == std::wstring::npos)
            space = filter.size();
        if (space > start)
        {
            std::string term = ToUtf8(filter.substr(start, space - start));
            for (char& c : term)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find(term) == std::string::npos)
                return false;
        }
        if (space >= filter.size())
            break;
        start = space + 1;
    }
    return true;
}

// --- the group menu component -----------------------------------------------
void GroupMenuRebuild(GroupMenu& menu);

void GroupMenuOpen(GroupMenu& menu, std::string title, std::string placeholder,
                   std::vector<GroupMenuItem> items)
{
    menu.title       = std::move(title);
    menu.placeholder = std::move(placeholder);
    menu.items       = std::move(items);
    menu.filter.clear();
    menu.selected = 0;
    menu.dirty    = true;
    menu.open     = true;
    GroupMenuRebuild(menu);   // so the selection is valid before the first frame
}

void GroupMenuRebuild(GroupMenu& menu)
{
    menu.rows.clear();
    menu.groups.clear();
    menu.matchCount = 0;
    menu.dirty = false;

    // Items arrive already ordered by group, so a group is a run of consecutive
    // items and one pass is enough.
    std::string current;
    int groupIndex = -1;

    for (int i = 0; i < static_cast<int>(menu.items.size()); ++i)
    {
        const GroupMenuItem& item = menu.items[static_cast<size_t>(i)];
        if (!MenuMatch(item.label, menu.filter))
            continue;

        ++menu.matchCount;

        if (item.group != current)
        {
            current = item.group;
            menu.groups.push_back(current);
            groupIndex = static_cast<int>(menu.groups.size()) - 1;
            menu.rows.push_back(GroupMenuRow{true, groupIndex, -1});
        }

        if (menu.collapsed.count(current) != 0)
            continue;
        menu.rows.push_back(GroupMenuRow{false, groupIndex, i});
    }

    // Keep the cursor on a real entry.
    if (menu.selected < 0 || menu.selected >= static_cast<int>(menu.rows.size()) ||
        menu.rows[static_cast<size_t>(menu.selected)].header)
    {
        menu.selected = 0;
        while (menu.selected < static_cast<int>(menu.rows.size()) &&
               menu.rows[static_cast<size_t>(menu.selected)].header)
            ++menu.selected;
    }
}

void GroupMenuMove(GroupMenu& menu, int delta)
{
    const int count = static_cast<int>(menu.rows.size());
    if (count == 0 || delta == 0)
        return;

    int target = menu.selected + delta;
    if (target < 0) target = 0;
    if (target >= count) target = count - 1;

    const int step = (delta > 0) ? 1 : -1;
    while (target >= 0 && target < count && menu.rows[static_cast<size_t>(target)].header)
        target += step;

    if (target >= 0 && target < count)
        menu.selected = target;
}

// First or last entry row, skipping the group band that may sit on the edge.
void GroupMenuSelectEdge(GroupMenu& menu, bool last)
{
    const int count = static_cast<int>(menu.rows.size());
    if (count == 0)
        return;

    int index = last ? count - 1 : 0;
    const int step = last ? -1 : 1;
    while (index >= 0 && index < count && menu.rows[static_cast<size_t>(index)].header)
        index += step;

    if (index >= 0 && index < count)
        menu.selected = index;
}

int GroupMenuSelectedId(const GroupMenu& menu)
{
    const int index = menu.selected;
    if (index < 0 || index >= static_cast<int>(menu.rows.size()))
        return -1;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(index)];
    if (row.header)
        return -1;
    return menu.items[static_cast<size_t>(row.item)].id;
}

// Keys the component owns. The caller checks its own toggle key before calling.
MenuAction GroupMenuKeyDown(GroupMenu& menu, WPARAM key)
{
    switch (key)
    {
        case VK_ESCAPE: return MenuAction::Close;
        case VK_RETURN: return MenuAction::Activate;
        case VK_UP:     GroupMenuMove(menu, -1); break;
        case VK_DOWN:   GroupMenuMove(menu, 1);  break;
        case VK_NEXT:   GroupMenuMove(menu, 10); break;
        case VK_PRIOR:  GroupMenuMove(menu, -10); break;
        case VK_HOME:   GroupMenuSelectEdge(menu, false); break;
        case VK_END:    GroupMenuSelectEdge(menu, true);  break;
        case VK_BACK:
            if (!menu.filter.empty())
            {
                menu.filter.pop_back();
                menu.dirty = true;
            }
            break;
        default:
            break;
    }
    return MenuAction::None;
}

void GroupMenuChar(GroupMenu& menu, wchar_t character)
{
    if (character >= 0x20 && character != 0x7F)
    {
        menu.filter.push_back(character);
        menu.dirty = true;
    }
}

GroupMenu* OpenMenu(AppState& app)
{
    // Il piu' profondo per primo: un controllo `Choice` aperto sta sopra le
    // opzioni, e finche' c'e' e' lui a prendere tastiera e clic.
    if (app.optionsChoice.open) return &app.optionsChoice;
    if (app.options.open)       return &app.options;
    if (app.palette.open)       return &app.palette;
    if (app.goTo.open)          return &app.goTo;
    if (app.columnMenu.open)    return &app.columnMenu;
    return nullptr;
}

GroupMenu* OpenMenu(AppState& app);
void       GroupMenuOpen(GroupMenu& menu, std::string title, std::string placeholder,
                         std::vector<GroupMenuItem> items);
const char* CommandLabelIt(CommandId id);
void       RunCommand(AppState& app, int command);
void       ReportStatus(AppState& app, const std::wstring& message);
void       RememberSettings(AppState& app);

// Cosa fa l'Invio, o un clic, dentro il pannello dei valori. Non lo chiude: una
// lista di caselle si spunta **a piu' riprese**, ed e' tutto il motivo per cui e'
// una lista e non un singolo interruttore.
void RunColumnMenuSelection(AppState& app)
{
    if (app.columnPanel < 0)
        return;
    const int id = GroupMenuSelectedId(app.columnMenu);
    auto& column = app.columns[static_cast<size_t>(app.columnPanel)];

    if (id == kFilterAllId)
    {
        column.excluded.clear();
    }
    else if (id == kFilterNoneId)
    {
        for (const std::pair<std::string, int>& value : app.columnPanelValues)
            column.excluded.insert(value.first);
    }
    else if (id >= 0 && id < static_cast<int>(app.columnPanelValues.size()))
    {
        const std::string& value = app.columnPanelValues[static_cast<size_t>(id)].first;
        if (column.excluded.count(value) != 0)
            column.excluded.erase(value);
        else
            column.excluded.insert(value);
    }

    RefreshColumnMenuChecks(app);
    app.viewDirty = true;
    app.scroll    = 0.0f;
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

bool IsBookmarked(const AppState& app)
{
    return std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory) != app.bookmarks.end();
}

void ToggleBookmark(AppState& app)
{
    if (app.directory.empty())
        return;
    const auto found = std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory);
    if (found != app.bookmarks.end())
        app.bookmarks.erase(found);
    else
        app.bookmarks.push_back(app.directory);
}

// --- building the two menus -------------------------------------------------
void BuildPaletteMenu(AppState& app)
{
    std::vector<GroupMenuItem> items;
    for (const CommandDef& command : kCommandTable)
    {
        // L'etichetta della tabella e' quella inglese, che e' anche il nome della
        // chiave; in italiano la da' `CommandLabelIt`, e se non c'e' resta quella.
        // In inglese vale l'etichetta della tabella; in italiano la traduzione, se
        // c'e'. Il ripiego e' sempre la tabella: una traduzione mancante lascia la
        // parola in inglese invece di lasciare il vuoto.
        const char* italian = CommandLabelIt(command.id);
        std::string label = (gLanguage == Language::English || italian == nullptr)
                                ? command.label
                                : italian;
        // The one command whose wording depends on state. The table stays data.
        if (command.id == CmdToggleBookmark && IsBookmarked(app))
            label = Tr("Togli questo posto dai segnalibri", "Remove bookmark from this folder");
        items.push_back(GroupMenuItem{command.group, label, "", command.id});
    }

    // Locations are data, not code: one entry per known folder, so the palette
    // covers the sidebar without a command being declared for each.
    for (int i = 0; i < static_cast<int>(app.places.size()); ++i)
    {
        items.push_back(GroupMenuItem{"Places", app.places[static_cast<size_t>(i)].label, "",
                                      kPalettePlaceBase + i});
    }

    app.goTo.open = false;   // one menu at a time
    app.options.open = false;
    GroupMenuOpen(app.palette, Tr("PALETTE DEI COMANDI", "COMMAND PALETTE"),
                  Tr("Scrivi per filtrare i comandi...", "Type to filter commands..."),
                  std::move(items));
    app.palette.hint = Tr("Invio esegue   Esc chiude   Ctrl+Maiusc+P apre e chiude   Ctrl+, opzioni",
                          "Enter runs   Esc closes   Ctrl+Shift+P toggles   Ctrl+, options");
}

void BuildGoToMenu(AppState& app)
{
    std::vector<GroupMenuItem> items;
    app.goToTargets.clear();

    const auto add = [&items, &app](const char* group, const std::wstring& path, const std::string& label)
    {
        const int id = static_cast<int>(app.goToTargets.size());
        items.push_back(GroupMenuItem{group, label, ToUtf8(path), id});
        app.goToTargets.push_back(path);
    };

    // The path itself, deepest last: the breadcrumb, reachable from the keyboard.
    if (!app.directory.empty())
    {
        std::vector<std::wstring> ancestors;
        std::wstring current = app.directory;
        for (;;)
        {
            ancestors.push_back(current);
            const std::wstring parent = ParentOf(current);
            if (parent == current)
                break;
            current = parent;
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
            add("Path", *it, ToUtf8(LeafName(*it)));
    }

    for (const Place& place : app.places)
        add("Places", place.path, place.label);

    for (const std::wstring& path : app.bookmarks)
        add("Bookmarks", path, ToUtf8(LeafName(path)));

    for (const std::wstring& path : app.recents)
        add("Recents", path, ToUtf8(LeafName(path)));

    app.palette.open = false;
    app.options.open = false;
    GroupMenuOpen(app.goTo, Tr("VAI A", "GO TO"),
                  Tr("Scrivi per filtrare le cartelle...", "Type to filter locations..."),
                  std::move(items));
    app.goTo.detailed = true;   // a location without its path is not much use
    app.goTo.hint = Tr("Invio va   Esc chiude   Ctrl+P apre e chiude",
                       "Enter goes   Esc closes   Ctrl+P toggles");
}

// --- operazioni su file (M7) ------------------------------------------------
int          CursorIndex(const AppState& app);
double       NowSeconds();

// Percorsi completi di cio' che e' selezionato **e visibile**, nell'ordine della
// lista.
//
// Filtrare puo' nascondere righe che restano nella selezione: la selezione e'
// per nome e sopravvive al cambio di filtro. Senza questo controllo il menu
// contestuale offriva "Elimina definitivamente" su un file che l'utente non
// vedeva piu', e un'azione su qualcosa che non si vede e' esattamente cio' che
// non deve poter succedere. La regola e' quella che Ctrl+A gia' segue: il filtro
// fa parte della domanda, quindi fa parte della risposta.
std::vector<std::wstring> SelectedPaths(const AppState& app)
{
    std::vector<std::wstring> paths;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (app.selection.count(entry.wideName) != 0)
            paths.push_back(EntryLocation(app, entry));
    }
    return paths;
}

// Un'operazione che fallisce in silenzio e' peggio di una che non parte: senza
// un messaggio, "Sposta" su un file in uso sembrerebbe non aver fatto nulla.
void ReportStatus(AppState& app, const std::wstring& message)
{
    app.statusMessage     = message;
    app.statusMessageTime = NowSeconds();
}

// La selezione come PIDL, nello stesso ordine dei percorsi. Le voci normali non ne
// hanno uno, e va bene: per quelle il percorso basta ed e' la forma che le
// operazioni su file si aspettano. Le voci di spazio dei nomi invece il PIDL ce
// l'hanno per forza, ed e' l'unico modo di chiedere alla shell il loro menu.
std::vector<const BYTE*> SelectedPidls(const AppState& app)
{
    std::vector<const BYTE*> pidls;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (app.selection.count(entry.wideName) != 0 && !entry.pidl.empty())
            pidls.push_back(entry.pidl.data());
    }
    return pidls;
}

// I comandi che agiscono su un **percorso** non valgono dove un percorso non c'e':
// nel cestino le voci si spostano, si cancellano e si ripristinano coi verbi della
// shell, che quegli oggetti li conoscono. Qui si dice, invece di far finta che sia
// un guasto e lasciare l'utente a chiedersi perche' non succede niente.
bool RefuseInNamespace(AppState& app, const wchar_t* action)
{
    if (!ShellLocation::IsNamespace(app.directory))
        return false;
    ReportStatus(app, std::wstring(action) +
                          L": qui non si puo', questa cartella non e' un percorso. "
                          L"Le voci si gestiscono coi comandi del menu contestuale.");
    return true;
}

// Ricarica la cartella mantenendo la selezione sui nomi che esistono ancora.
//
// LoadDirectory svuota la selezione e sceglie la prima riga, ed e' giusto perche'
// cambia cartella. Dopo un incolla o un'eliminazione no: l'utente sta lavorando
// li' e perdere il posto a ogni operazione lo costringe a ricercarlo.
void ReloadKeeping(AppState& app, const std::unordered_set<std::wstring>& keep,
                   const std::wstring& preferCursor, float restoreScroll)
{
    LoadDirectory(app, app.directory, /*recordHistory=*/false);

    const auto present = [&](const std::wstring& name) {
        for (const Entry& entry : app.entries)
            if (entry.wideName == name)
                return true;
        return false;
    };

    app.selection.clear();
    for (const Entry& entry : app.entries)
        if (keep.count(entry.wideName) != 0)
            app.selection.insert(entry.wideName);

    if (!preferCursor.empty() && present(preferCursor))
    {
        app.cursor = preferCursor;
        if (app.selection.empty())
            app.selection.insert(preferCursor);
        app.selectionAnchor = preferCursor;
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

// Nome predefinito di una cartella nuova, come quello di Esplora risorse.
constexpr wchar_t kNewFolderName[] = L"Nuova cartella";

void BeginRename(AppState& app)
{
    const int index = CursorIndex(app);
    if (index < 0)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(index)];
    app.renameActive   = true;
    app.renamePath     = JoinPath(app.directory, entry.wideName);
    app.renameOriginal = entry.wideName;
    app.renameText     = entry.name;

    // La tastiera adesso scrive nel nome, non nel filtro: senza questo, rinominare
    // in "foto" filtrerebbe la lista a ogni lettera.
    app.filterActive = false;
}

void CancelRename(AppState& app)
{
    app.renameActive = false;
    app.renameText.clear();
    app.renamePath.clear();
    app.renameOriginal.clear();
}

void CommitRename(AppState& app)
{
    if (!app.renameActive)
        return;

    const std::wstring newName  = Widen(app.renameText);
    const std::wstring path     = app.renamePath;
    const std::wstring original = app.renameOriginal;
    const float        scroll   = app.scroll;
    CancelRename(app);

    if (newName.empty() || newName == original)
        return;

    std::wstring error;
    std::vector<std::wstring> created;
    const FileOps::Result result = FileOps::Rename(app.window, path, newName, &created, &error);
    if (result == FileOps::Result::Ok)
    {
        std::unordered_set<std::wstring> keep;
        const std::wstring landed = created.empty() ? newName : LeafName(created.front());
        keep.insert(landed);
        ReloadKeeping(app, keep, landed, scroll);
        ReportStatus(app, L"Rinominato in " + landed);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Rinomina non riuscita: " + error);
    }
}

void DoCopyToClipboard(AppState& app, bool cut)
{
    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    if (!FileOps::SetClipboardFiles(app.window, paths, cut))
    {
        ReportStatus(app, L"Clipboard non disponibile.");
        return;
    }

    app.clipboardCut = cut;
    ReportStatus(app, (cut ? L"Tagliati " : L"Copiati ") +
                          std::to_wstring(paths.size()) + L" elementi");
}

void DoPaste(AppState& app)
{
    std::vector<std::wstring> paths;
    bool cut = false;
    if (!FileOps::ReadClipboardFiles(&paths, &cut))
    {
        ReportStatus(app, L"Niente da incollare.");
        return;
    }

    const float scroll = app.scroll;
    std::wstring error;
    std::vector<std::wstring> created;
    // Nel registro, perche' una volta un incolla e' partito senza che si riuscisse a
    // capire da quale gesto: senza una riga, l'unica traccia era un messaggio di
    // stato che dura quattro secondi.
    DiagLog("  incolla: " + std::to_string(paths.size()) + " percorsi, " +
            (cut ? "tagliati" : "copiati") + ", in " + ToUtf8(LeafName(app.directory)));
    const FileOps::Result result = cut ? FileOps::Move(app.window, paths, app.directory, &created, &error)
                                       : FileOps::Copy(app.window, paths, app.directory, &created, &error);

    if (result == FileOps::Result::Ok)
    {
        // Un taglio si consuma con l'incolla: lasciarlo nella clipboard farebbe
        // spostare gli stessi file una seconda volta al prossimo Ctrl+V.
        if (cut)
            FileOps::ClearClipboard(app.window);

        // Si seleziona cio' che la shell ha DAVVERO creato. Con un conflitto di
        // nome il nome finale non e' quello di partenza, e selezionare quello di
        // partenza metteva il cursore sull'originale: un Canc subito dopo
        // cancellava la cosa sbagliata.
        std::unordered_set<std::wstring> keep;
        std::wstring landed;
        for (const std::wstring& path : created)
        {
            const std::wstring name = LeafName(path);
            keep.insert(name);
            if (landed.empty())
                landed = name;
        }
        if (keep.empty())
        {
            for (const std::wstring& path : paths)
                keep.insert(LeafName(path));
        }

        ReloadKeeping(app, keep, landed, scroll);
        ReportStatus(app, (cut ? L"Spostati " : L"Incollati ") +
                              std::to_wstring(paths.size()) + L" elementi");
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Incolla non riuscito: " + error);
    }
}

void DoDelete(AppState& app, bool permanently)
{
    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    const float scroll = app.scroll;
    std::wstring error;
    const FileOps::Result result = FileOps::Delete(app.window, paths, permanently, &error);

    if (result == FileOps::Result::Ok)
    {
        ReloadKeeping(app, /*keep=*/{}, /*preferCursor=*/{}, scroll);
        ReportStatus(app, (permanently ? L"Eliminati definitivamente " : L"Nel cestino: ") +
                              std::to_wstring(paths.size()) + L" elementi");
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Eliminazione non riuscita: " + error);
    }
}

void DoNewFolder(AppState& app)
{
    const float scroll = app.scroll;
    std::vector<std::wstring> created;
    std::wstring error;
    const FileOps::Result result = FileOps::CreateFolder(
        app.window, app.directory, kNewFolderName, &created, &error);

    if (result == FileOps::Result::Ok)
    {
        const std::wstring name = created.empty() ? std::wstring(kNewFolderName)
                                                  : LeafName(created.front());
        std::unordered_set<std::wstring> keep;
        keep.insert(name);
        ReloadKeeping(app, keep, name, scroll);

        // Creare e rinominare sono un'azione sola: chi preme Ctrl+Shift+N vuole
        // dare un nome, non guardare un'altra "Nuova cartella".
        BeginRename(app);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Cartella non creata: " + error);
    }
}

// Il contenuto della voce nuova. La shell crea il file e sceglie il nome — e' il
// solo modo di avere il "(2)" dei conflitti senza indovinarlo — ma senza un
// modello crea un file di **zero byte**: e uno zip di zero byte non e' uno zip,
// un .docx di zero byte non si apre. Il contenuto lo dice il registro, e lo
// scriviamo qui.
//
// I modelli di Office, per esempio, sono registrati con un **guscio vuoto** nella
// loro cartella VFS (`WORD.DOCX` e' un file di zero byte su disco): il contenuto
// vero lo produce il provider di Office, che da fuori non si raggiunge. Il file
// nuovo nasce vuoto anche a noi, e allora lo si dice — un .docx che Word rifiuta
// di aprire, senza una parola, e' peggio di un .docx che non c'e'.
void FillNewItemContent(AppState& app, const ShellNew::Template& model,
                        const std::wstring& path)
{
    if (model.source == ShellNew::Template::Source::Empty)
        return;

    if (model.source == ShellNew::Template::Source::TemplateFile)
    {
        // Copia, invece di leggere e riscrivere: il modello puo' essere grande
        // (un .pub sono sessanta kilobyte), e questa e' una copia di file sola.
        if (!CopyFileW(model.templatePath.c_str(), path.c_str(), FALSE))
        {
            ReportStatus(app, L"Il modello di questo tipo non si e' potuto copiare: "
                              L"il file e' stato creato vuoto.");
            return;
        }
    }
    else
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            ReportStatus(app, L"Il file e' stato creato, ma il contenuto del modello no.");
            return;
        }

        DWORD written = 0;
        const bool wrote =
            WriteFile(file, model.data.data(), static_cast<DWORD>(model.data.size()), &written,
                      nullptr) != FALSE &&
            written == model.data.size();
        CloseHandle(file);
        if (!wrote)
        {
            ReportStatus(app, L"Il file e' stato creato, ma il contenuto del modello no.");
            return;
        }
    }

    // Il modello c'era ma era vuoto: il file e' nato vuoto, e chi lo apre lo deve
    // sapere adesso e non quando l'applicazione gli dira' che e' danneggiato.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow == 0)
    {
        ReportStatus(app, L"Il modello di questo tipo e' vuoto: " + LeafName(path) +
                              L" e' stato creato vuoto.");
    }
}

// Crea una voce nuova dal modello |index| del registro, col meccanismo del
// sistema: il nome definitivo lo sceglie la shell ("Nuovo documento di testo.txt",
// e "(2)" se esiste gia'), il contenuto lo mette `FillNewItemContent`.
void CreateNewItem(AppState& app, size_t index)
{
    const std::vector<ShellNew::Template>& templates = ShellNew::Templates();
    if (index >= templates.size())
        return;

    const std::wstring wanted = templates[index].suggestedName;
    const float scroll = app.scroll;
    std::vector<std::wstring> created;
    std::wstring error;
    const FileOps::Result result =
        FileOps::NewItem(app.window, app.directory, wanted, &created, &error);

    if (result != FileOps::Result::Ok)
    {
        if (result == FileOps::Result::Failed)
            ReportStatus(app, L"File non creato: " + error);
        return;
    }

    const std::wstring path = created.empty() ? JoinPath(app.directory, wanted)
                                              : created.front();
    FillNewItemContent(app, templates[index], path);

    const std::wstring name = LeafName(path);
    std::unordered_set<std::wstring> keep;
    keep.insert(name);
    ReloadKeeping(app, keep, name, scroll);

    // Creare e rinominare sono un'azione sola, come per la cartella: chi crea
    // vuole dare un nome, non guardare un altro "Nuovo documento di testo".
    BeginRename(app);
}

// --- menu execution ---------------------------------------------------------
void RunCommand(AppState& app, int command);

void RunPaletteSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.palette);
    if (id < 0)
        return;

    if (id >= kPalettePlaceBase)
    {
        const int place = id - kPalettePlaceBase;
        if (place < static_cast<int>(app.places.size()))
            LoadDirectory(app, app.places[static_cast<size_t>(place)].path);
        app.palette.open = false;
        return;
    }

    RunCommand(app, id);
    if (id != CmdToggleDetails)
        app.palette.open = false;
}

void RunGoToSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.goTo);
    if (id < 0 || id >= static_cast<int>(app.goToTargets.size()))
        return;
    LoadDirectory(app, app.goToTargets[static_cast<size_t>(id)]);
    app.goTo.open = false;
}

// --- il pannello delle opzioni (M9) -----------------------------------------
//
// Il riferimento non mette le preferenze dentro la lista dei comandi: ha un
// **pannello di opzioni**, una lista cercabile in cui ogni riga porta tre cose —
// il nome, la ragione e il controllo del tipo giusto. Il componente c'e' gia':
// `GroupMenu` filtra, raggruppa, naviga con Su/Giu' e si chiude con Esc. Quello
// che gli mancava sono i **controlli tipizzati**, ed e' quello che si aggiunge
// qui.
//
// La verita' di un'opzione sta in `AppState`: `RefreshOptionItem` e' l'unico
// posto che la porta nella riga, e `ApplyOptionFromItem` l'unico che la riporta
// indietro. Cosi' non esistono due copie di "i suggerimenti sono accesi".
enum OptionId
{
    OptFont, OptFontSize, OptInspectorFont, OptInspectorFontSize,
    OptSpacing, OptZoom, OptAnimations, OptRoundCorners, OptSystemScheme,
    OptColorScheme, OptAlternatingRows, OptInactiveOpacity, OptFileExtension,
    OptLongPaths, OptCompactPaths,
    OptTooltips, OptHoverToSelect, OptUnderlineOnHover, OptSingleClick,
    OptSingleClickFoldersOnly, OptActOnPress, OptDoubleClickToParent,
    OptShowCheckboxes, OptSidebar, OptGridIconSize, OptLanguage,
};

// L'altezza di una riga del pannello: due linee di testo, e **non** `Spacing`.
// Se seguisse la preferenza che sta comandando, lo slider si tirerebbe dietro le
// proprie righe e il bersaglio si sposterebbe mentre lo si trascina.
constexpr float kOptionRowHeight = 46.0f;

float MenuRowHeight(const Layout& layout, const GroupMenu& menu)
{
    return menu.richRows ? kOptionRowHeight * layout.scale : layout.rowHeight;
}

// La prima riga visibile: la stessa formula per il disegno e per il clic, o le
// due non sarebbero d'accordo su cosa sta sotto il puntatore.
int GroupMenuFirstVisible(const GroupMenu& menu, float listTop, float listBottom, float rowHeight)
{
    const int visible = (std::max)(1, static_cast<int>((listBottom - listTop) / rowHeight));
    int first = 0;
    if (menu.selected >= first + visible)
        first = menu.selected - visible + 1;
    if (menu.selected < first)
        first = menu.selected;
    return first;
}

// Un nome di carattere esiste? `CreateFontW` sostituisce in silenzio quello che
// non conosce, quindi la domanda non e' se la chiamata riesce: e' se la faccia
// che torna e' quella chiesta. Senza questo controllo, un nome sbagliato
// cambierebbe il carattere di tutta l'interfaccia senza dirlo.
bool FontInstalled(const std::wstring& face)
{
    if (face.empty())
        return false;
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return false;
    HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, face.c_str());
    bool installed = false;
    if (font != nullptr)
    {
        HGDIOBJ previous = SelectObject(dc, font);
        wchar_t actual[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, actual);
        installed = _wcsicmp(actual, face.c_str()) == 0;
        SelectObject(dc, previous);
        DeleteObject(font);
    }
    DeleteDC(dc);
    return installed;
}

// L'atlante dei glifi si rifa' solo quando una delle tre misure cambia davvero:
// rasterizzarlo a ogni fotogramma sarebbe un lavoro che nessuno vede.
void ApplyFonts(AppState& app)
{
    std::string error;
    const int smallSize = (std::max)(AppState::kUiFontSizeMin, app.uiFontSize - 2);
    app.font.Build(app.renderer, app.fontFace.c_str(), app.uiFontSize, kGlyphLayer,
                   kTextureLayerSize, &error);
    app.smallFont.Build(app.renderer, app.fontFace.c_str(), smallSize, kSmallGlyphLayer,
                        kTextureLayerSize, &error);
    app.monoFontOk = app.monoFont.Build(app.renderer, app.monoFace.c_str(), app.monoFontSize,
                                        kMonoLayer, kTextureLayerSize, &error);
}

void RefreshOptionItem(const AppState& app, GroupMenuItem& item)
{
    item.enabled = true;
    switch (static_cast<OptionId>(item.id))
    {
        case OptFont:
            item.control   = OptionControl::Text;
            item.valueText = ToUtf8(app.fontFace);
            break;
        case OptFontSize:
            item.control   = OptionControl::Stepper;
            item.valueText = std::to_string(app.uiFontSize);
            break;
        case OptInspectorFont:
            item.control   = OptionControl::Text;
            item.valueText = ToUtf8(app.monoFace);
            break;
        case OptInspectorFontSize:
            item.control   = OptionControl::Stepper;
            item.valueText = std::to_string(app.monoFontSize);
            break;
        case OptSpacing:
            item.control  = OptionControl::Slider;
            item.fraction = (app.rowSpacing - AppState::kRowSpacingMin) /
                            (AppState::kRowSpacingMax - AppState::kRowSpacingMin);
            item.valueText = std::to_string(static_cast<int>(app.rowSpacing + 0.5f));
            break;
        case OptZoom:
            item.control  = OptionControl::Slider;
            item.fraction = (app.zoom - 0.6f) / 1.4f;
            item.valueText = std::to_string(static_cast<int>(app.zoom * 100.0f + 0.5f)) + "%";
            break;
        case OptAlternatingRows:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.alternatingRows;
            break;
        case OptTooltips:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.showTooltips;
            break;
        case OptHoverToSelect:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.hoverToSelect;
            break;
        case OptUnderlineOnHover:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.underlineOnHover;
            break;
        case OptSingleClick:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.singleClick;
            break;
        case OptSingleClickFoldersOnly:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.singleClickFoldersOnly;
            item.enabled = app.singleClick;   // da sola non vuol dire niente
            break;
        case OptActOnPress:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.actOnPress;
            break;
        case OptDoubleClickToParent:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.doubleClickToParent;
            break;
        case OptShowCheckboxes:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.showCheckboxes;
            break;
        case OptSidebar:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.sidebarVisible;
            break;
        case OptGridIconSize:
            item.control  = OptionControl::Slider;
            item.fraction = (app.gridIconSize - AppState::kGridIconMin) /
                            (AppState::kGridIconMax - AppState::kGridIconMin);
            item.valueText = std::to_string(static_cast<int>(app.gridIconSize));
            break;
        case OptLanguage:
            item.control   = OptionControl::Choice;
            item.valueText = (gLanguage == Language::English) ? "English" : "Italiano";
            break;
        // Le opzioni che il riferimento ha e che qui non ci sono. Si mostrano
        // **spente**: un'opzione che non si puo' applicare sta in vista e dice
        // che non si tocca, invece di sparire e far credere che non esista.
        case OptAnimations:
        case OptRoundCorners:
        case OptSystemScheme:
        case OptLongPaths:
            item.control  = OptionControl::Toggle;
            item.toggleOn = true;
            item.enabled  = false;
            break;
        case OptColorScheme:
            item.control   = OptionControl::Choice;
            item.valueText = "Voidstar";
            item.enabled   = false;
            break;
        case OptInactiveOpacity:
            item.control  = OptionControl::Slider;
            item.fraction = 0.45f;
            item.valueText = "45%";
            item.enabled  = false;
            break;
        case OptFileExtension:
            item.control  = OptionControl::Toggle;
            item.toggleOn = true;
            item.enabled  = false;
            break;
        case OptCompactPaths:
            item.control  = OptionControl::Toggle;
            item.toggleOn = false;
            item.enabled  = false;
            break;
        default:
            break;
    }
}

void RefreshOptionItems(const AppState& app, GroupMenu& menu)
{
    for (GroupMenuItem& item : menu.items)
        RefreshOptionItem(app, item);
}

void BuildOptionsMenu(AppState& app);

void ApplyOptionFromItem(AppState& app, GroupMenuItem& item)
{
    const OptionId id = static_cast<OptionId>(item.id);
    // `BuildOptionsMenu` ricostruisce l'elenco, quindi ogni riferimento a `item`
    // scade da li' in poi: l'id si prende subito.
    const int changedId = item.id;
    bool fontsChanged = false;
    bool saved        = true;

    const auto clampInt = [](int value, int low, int high) {
        return value < low ? low : (value > high ? high : value);
    };

    switch (id)
    {
        case OptFont:
        case OptInspectorFont:
        {
            const std::wstring face = Widen(item.valueText);
            if (!FontInstalled(face))
            {
                ReportStatus(app, TrW(L"Carattere inesistente: il nome non cambia",
                                      L"No such font: the name is unchanged"));
                saved = false;
                break;
            }
            if (id == OptFont) app.fontFace = face;
            else                app.monoFace = face;
            fontsChanged = true;
            break;
        }
        case OptFontSize:
        {
            const int size = clampInt(atoi(item.valueText.c_str()),
                                      AppState::kUiFontSizeMin, AppState::kUiFontSizeMax);
            if (size != app.uiFontSize)
            {
                app.uiFontSize = size;
                fontsChanged   = true;
            }
            break;
        }
        case OptInspectorFontSize:
        {
            const int size = clampInt(atoi(item.valueText.c_str()),
                                      AppState::kMonoFontSizeMin, AppState::kMonoFontSizeMax);
            if (size != app.monoFontSize)
            {
                app.monoFontSize = size;
                fontsChanged     = true;
            }
            break;
        }
        case OptSpacing:
        {
            const float value = AppState::kRowSpacingMin +
                                item.fraction * (AppState::kRowSpacingMax - AppState::kRowSpacingMin);
            app.rowSpacing = std::round(value);
            break;
        }
        case OptZoom:
            // A passi di 5%: uno zoom che cambia di un millesimo non si vede,
            // e riempirebbe il file di impostazioni di cifre inutili.
            app.zoom = std::round((0.6f + item.fraction * 1.4f) * 20.0f) / 20.0f;
            break;
        case OptGridIconSize:
            app.gridIconSize = std::round((AppState::kGridIconMin +
                                           item.fraction * (AppState::kGridIconMax - AppState::kGridIconMin)) /
                                          2.0f) * 2.0f;
            break;
        case OptAlternatingRows: app.alternatingRows = item.toggleOn; break;
        case OptTooltips:        app.showTooltips     = item.toggleOn; break;
        case OptHoverToSelect:   app.hoverToSelect    = item.toggleOn; break;
        case OptUnderlineOnHover: app.underlineOnHover = item.toggleOn; break;
        case OptSingleClick:     app.singleClick      = item.toggleOn; break;
        case OptSingleClickFoldersOnly: app.singleClickFoldersOnly = item.toggleOn; break;
        case OptActOnPress:      app.actOnPress       = item.toggleOn; break;
        case OptDoubleClickToParent: app.doubleClickToParent = item.toggleOn; break;
        case OptShowCheckboxes:  app.showCheckboxes   = item.toggleOn; break;
        case OptSidebar:         app.sidebarVisible   = item.toggleOn; break;
        case OptLanguage:
            // Le due lingue sono due voci, non un interruttore: la scelta mostra
            // sempre quale delle due e' accesa.
            gLanguage = (item.valueText == "English") ? Language::English : Language::Italian;
            break;
        default:
            saved = false;   // una riga spenta non ha niente da applicare
            break;
    }

    if (fontsChanged)
        ApplyFonts(app);

    // Le etichette e le descrizioni del pannello sono scritte in una lingua:
    // cambiandola, le righe costruite prima restano nella lingua vecchia. Si rifa'
    // l'elenco e si rimette il cursore dove era, invece di riportarlo in cima.
    if (id == OptLanguage)
    {
        const std::wstring filter = app.options.filter;
        BuildOptionsMenu(app);
        app.options.filter = filter;
        app.options.dirty  = true;
        GroupMenuRebuild(app.options);
        for (int i = 0; i < static_cast<int>(app.options.rows.size()); ++i)
        {
            const GroupMenuRow& row = app.options.rows[static_cast<size_t>(i)];
            if (!row.header && app.options.items[static_cast<size_t>(row.item)].id == changedId)
            {
                app.options.selected = i;
                break;
            }
        }
    }
    else
    {
        RefreshOptionItems(app, app.options);
    }

    if (saved)
        RememberSettings(app);
    app.viewDirty = true;
}

// Il pannello vero e proprio. Una lista piatta, come nel riferimento: il campo in
// alto filtra, e i gruppi sarebbero una seconda gerarchia sopra una lista che si
// cerca gia' per nome.
void BuildOptionsMenu(AppState& app)
{
    struct OptionRow
    {
        const char*  label;
        const char*  descriptionIt;
        const char*  descriptionEn;
        OptionId     id;
        bool         notApplied;   // il riferimento la ha, qui non e' applicata
    };
    static const OptionRow kRows[] = {
        {"Font", "Il nome di un carattere installato, applicato a tutta l'interfaccia.",
         "The system-installed font name that is applied to the entire UI.", OptFont, false},
        {"Font Size", "La misura del testo dell'interfaccia.",
         "Defines the size of the text in the interface.", OptFontSize, false},
        {"Inspector Font", "Il carattere usato per il testo nell'inspector.",
         "The system-installed font used when previewing text contents in the inspector.", OptInspectorFont, false},
        {"Inspector Font Size", "La misura del testo nell'inspector.",
         "Specifies the font size used to display text contents in the inspector.", OptInspectorFontSize, false},
        {"Spacing", "Lo spazio fra le voci di una lista.",
         "Defines the space between items in a list.", OptSpacing, false},
        {"Zoom", "Ingrandisce o rimpicciolisce tutta l'interfaccia.",
         "Scales the whole interface up or down.", OptZoom, false},
        {"Animations", "Animazioni delle aperture, dello scorrimento e del cursore di testo.",
         "Applies animations to new popups, view scrolling, text cursor, etc.", OptAnimations, true},
        {"Round Corners", "Arrotonda gli angoli di pannelli, pulsanti e selezioni.",
         "Applies rounding to the corners of UI elements such as popups, buttons, and selections.", OptRoundCorners, true},
        {"System Light / Dark Scheme", "Segue il tema chiaro o scuro del sistema.",
         "Automatically adapt between light and dark color schemes based on system settings.", OptSystemScheme, true},
        {"Color Scheme", "Applica una tavolozza di colori a tutta l'interfaccia.",
         "Applies a color palette to all UI elements.", OptColorScheme, true},
        {"Alternating Row Colors", "Colora a righe alterne per distinguere righe vicine.",
         "Applies alternating row colors to improve readability and distinguish adjacent rows.", OptAlternatingRows, false},
        {"Inactive Panel Content Opacity", "Attenua il contenuto del pannello che non ha il fuoco.",
         "Applies dimming to inactive panel contents to make the active panel stand out.", OptInactiveOpacity, true},
        {"Show File Extension", "Mostra o nasconde l'estensione nei nomi dei file.",
         "Toggle the visibility of file extensions.", OptFileExtension, true},
        {"Long Paths Support", "Percorsi oltre 260 caratteri.",
         "Enable support for paths longer than 260 characters.", OptLongPaths, true},
        {"Display Compact Paths", "Accorcia i percorsi lunghi nella barra del percorso.",
         "Displays compact paths in the toolbar.", OptCompactPaths, true},
        {"Show Tooltips", "Mostra un suggerimento quando il puntatore si ferma su una riga.",
         "Shows a hint when the pointer rests on a row.", OptTooltips, false},
        {"Hover To Select", "Seleziona la riga sotto il puntatore dopo una pausa.",
         "Selects the row under the pointer after a short pause.", OptHoverToSelect, false},
        {"Underline On Hover", "Sottolinea il nome della riga sotto il puntatore.",
         "Underlines the name of the row under the pointer.", OptUnderlineOnHover, false},
        {"Single Click", "Apre con un clic invece che con due.",
         "Opens an item with one click instead of two.", OptSingleClick, false},
        {"Single Click Folders Only", "Con l'apertura a un clic, sui file ne servono ancora due.",
         "With single click, still requires two clicks for files.", OptSingleClickFoldersOnly, false},
        {"Act On Press", "Agisce appena il pulsante scende, non quando risale.",
         "Acts as soon as the button goes down, not when it comes up.", OptActOnPress, false},
        {"Double Click On Empty Space Goes Up", "Doppio clic sul fondo: va alla cartella superiore.",
         "Goes to the parent folder when the empty space is double clicked.", OptDoubleClickToParent, false},
        {"Show Checkboxes", "Una casella di scelta su ogni riga.",
         "Shows a checkbox on every row.", OptShowCheckboxes, false},
        {"Show The Sidebar", "Mostra il riquadro dei segnalibri e dei posti a sinistra.",
         "Shows the bookmark and place panel on the left.", OptSidebar, false},
        {"Grid Icon Size", "La misura di una cella nelle viste a icone e a riquadri.",
         "The size of a cell in the icon and tile views.", OptGridIconSize, false},
        {"Language", "La lingua dell'interfaccia.",
         "The language of the interface. Applied immediately.", OptLanguage, false},
    };
    // Le righe che il riferimento ha e che qui non sono applicate: si mostrano
    // spente: un'opzione che non si puo' applicare sta in vista e dice che non si
    // tocca, invece di sparire e far credere che non esista. La ragione e' scritta
    // accanto alla descrizione, dove si legge.
    const char* notAppliedNote = Tr(" (non ancora applicata in Bolt)",
                                    " (not yet applied in Bolt)");

    std::vector<GroupMenuItem> items;
    items.reserve(sizeof(kRows) / sizeof(kRows[0]));
    for (const OptionRow& row : kRows)
    {
        GroupMenuItem item;
        item.group       = "";          // lista piatta, come il riferimento
        item.label       = row.label;
        item.description = std::string(Tr(row.descriptionIt, row.descriptionEn)) +
                           (row.notApplied ? notAppliedNote : "");
        item.id          = row.id;
        if (row.id == OptLanguage)
        {
            item.choices.push_back({"Italiano", 0});
            item.choices.push_back({"English", 1});
        }
        RefreshOptionItem(app, item);
        items.push_back(std::move(item));
    }

    app.palette.open = false;
    app.goTo.open    = false;
    app.optionsChoice.open = false;
    GroupMenuOpen(app.options, Tr("OPZIONI", "OPTIONS"),
                  Tr("Scegli un'opzione...", "Select an option..."), std::move(items));
    app.options.richRows = true;
    app.options.hint     = Tr("Su Giu' per navigare   Invio per usare   Esc per chiudere",
                              "Up Down to navigate   Enter to use   Escape to dismiss");
}

Rect MenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu);

// La riga sotto un punto, dentro il pannello delle opzioni.
int OptionRowAt(const Layout& layout, const GroupMenu& menu, const Rect& panel, float y)
{
    const float rowHeight  = MenuRowHeight(layout, menu);
    const float listTop    = panel.y + 58.0f * layout.scale;
    const float listBottom = panel.bottom() - 26.0f * layout.scale;
    if (y < listTop || y >= listBottom)
        return -1;
    const int first   = GroupMenuFirstVisible(menu, listTop, listBottom, rowHeight);
    const int visible = (std::max)(1, static_cast<int>((listBottom - listTop) / rowHeight));
    const int index   = first + static_cast<int>((y - listTop) / rowHeight);
    if (index >= first + visible || index < 0 || index >= static_cast<int>(menu.rows.size()))
        return -1;
    return index;
}

// Quanto e' largo il controllo di un tipo. La larghezza non dipende dalla riga:
// e' la stessa per il disegno, per il clic e per il trascinamento di uno slider.
float OptionControlWidth(const GroupMenuItem& item, float scale)
{
    switch (item.control)
    {
        case OptionControl::Text:    return 200.0f * scale;
        case OptionControl::Choice:  return 170.0f * scale;
        case OptionControl::Stepper: return 128.0f * scale;
        case OptionControl::Slider:  return 150.0f * scale;
        case OptionControl::Toggle:  return 40.0f * scale;
        default:                     return 0.0f;
    }
}

// Dove sta il controllo di una riga. Una funzione sola, perche' disegno e clic
// devono scegliere lo stesso rettangolo.
Rect OptionControlRect(const Layout& layout, const GroupMenuItem& item, const Rect& rowRect)
{
    const float scale = layout.scale;
    const float height = 22.0f * scale;
    const float width  = OptionControlWidth(item, scale);
    return Rect{rowRect.right() - width - 14.0f * scale,
                rowRect.y + (rowRect.h - height) * 0.5f, width, height};
}

void BeginOptionEdit(AppState& app, const GroupMenuItem& item)
{
    app.optionEditActive = true;
    app.optionEditId     = item.id;
    app.optionEditText   = item.valueText;
}

void CancelOptionEdit(AppState& app)
{
    app.optionEditActive = false;
    app.optionEditId     = -1;
    app.optionEditText.clear();
    RefreshOptionItems(app, app.options);
}

void CommitOptionEdit(AppState& app)
{
    if (!app.optionEditActive)
        return;
    const int id = app.optionEditId;
    std::string text = app.optionEditText;
    app.optionEditActive = false;
    app.optionEditId     = -1;
    app.optionEditText.clear();

    // Gli spazi ai bordi non fanno parte di un nome di carattere.
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.erase(text.begin());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.pop_back();

    for (GroupMenuItem& item : app.options.items)
    {
        if (item.id == id)
        {
            item.valueText = text;
            ApplyOptionFromItem(app, item);
            return;
        }
    }
}

void OpenOptionChoice(AppState& app, const GroupMenuItem& item)
{
    std::vector<GroupMenuItem> items;
    for (const std::pair<std::string, int>& choice : item.choices)
        items.push_back(GroupMenuItem{"", choice.first, "", choice.second});

    app.optionsChoiceParent = item.id;
    GroupMenuOpen(app.optionsChoice, item.label, Tr("Filtra i valori...", "Filter values..."),
                  std::move(items));
    app.optionsChoice.richRows = false;
    app.optionsChoice.hint =
        Tr("Invio sceglie   Esc torna al pannello", "Enter chooses   Esc goes back");
}

void RunOptionsChoiceSelection(AppState& app)
{
    const int parent = app.optionsChoiceParent;
    const int value  = GroupMenuSelectedId(app.optionsChoice);
    app.optionsChoice.open = false;
    if (parent < 0 || value < 0)
        return;

    for (GroupMenuItem& item : app.options.items)
    {
        if (item.id != parent)
            continue;
        for (const std::pair<std::string, int>& choice : item.choices)
            if (choice.second == value)
                item.valueText = choice.first;
        ApplyOptionFromItem(app, item);
        return;
    }
}

// I tasti che riguardano il **controllo** della riga scelta. Ritorna vero se il
// tasto e' stato consumato: Su/Giu'/Home/Fine restano al componente.
bool OptionsKeyDown(AppState& app, GroupMenu& menu, WPARAM key)
{
    if (menu.selected < 0 || menu.selected >= static_cast<int>(menu.rows.size()))
        return false;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(menu.selected)];
    if (row.header || row.item < 0)
        return false;
    GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];

    if (!item.enabled)
        return key == VK_RETURN || key == VK_SPACE;

    switch (key)
    {
        case VK_RETURN:
        case VK_SPACE:
            if (item.control == OptionControl::Toggle)
            {
                item.toggleOn = !item.toggleOn;
                ApplyOptionFromItem(app, item);
            }
            else if (item.control == OptionControl::Choice)
            {
                OpenOptionChoice(app, item);
            }
            else if (item.control == OptionControl::Text)
            {
                BeginOptionEdit(app, item);
            }
            // Uno stepper e uno slider non hanno un "invio": si muovono con le
            // frecce, che sono il gesto di chi cambia un numero.
            return true;

        case VK_LEFT:
        case VK_RIGHT:
        {
            const int delta = (key == VK_RIGHT) ? 1 : -1;
            if (item.control == OptionControl::Stepper)
            {
                item.valueText = std::to_string(atoi(item.valueText.c_str()) + delta);
                ApplyOptionFromItem(app, item);
                return true;
            }
            if (item.control == OptionControl::Slider)
            {
                float fraction = item.fraction + delta * 0.05f;
                fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
                item.fraction = fraction;
                ApplyOptionFromItem(app, item);
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Un clic dentro il pannello delle opzioni. Non chiude il pannello: si cambiano
// piu' preferenze di seguito, ed e' tutto il motivo per cui e' un pannello e non
// un singolo comando.
MenuAction OptionsClick(AppState& app, const Layout& layout, GroupMenu& menu, float x, float y)
{
    if (!menu.open)
        return MenuAction::None;

    const Rect panel = MenuPanel(app, layout, menu);
    if (!panel.Contains(x, y))
        return MenuAction::Close;

    const int index = OptionRowAt(layout, menu, panel, y);
    if (index < 0 || menu.rows[static_cast<size_t>(index)].header)
        return MenuAction::None;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(index)];
    if (row.item < 0)
        return MenuAction::None;

    menu.selected = index;
    GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];
    if (!item.enabled)
        return MenuAction::None;

    const float rowHeight = MenuRowHeight(layout, menu);
    const float listTop   = panel.y + 58.0f * layout.scale;
    const int   first     = GroupMenuFirstVisible(menu, listTop, panel.bottom() - 26.0f * layout.scale,
                                                  rowHeight);
    const Rect  rowRect{panel.x + 6.0f * layout.scale,
                        listTop + (index - first) * rowHeight,
                        panel.w - 12.0f * layout.scale, rowHeight};
    const Rect  control = OptionControlRect(layout, item, rowRect);

    // Cambiare una preferenza vuole il puntatore **sul controllo**. Un clic sul
    // resto della riga la sceglie e basta: sulla riga di `Spacing` un clic a
    // sinistra avrebbe messo lo slider a zero, che non e' quello che si chiede
    // cliccando un'etichetta.
    if (!control.Contains(x, y))
        return MenuAction::None;

    switch (item.control)
    {
        case OptionControl::Toggle:
            item.toggleOn = !item.toggleOn;
            ApplyOptionFromItem(app, item);
            break;
        case OptionControl::Text:
            BeginOptionEdit(app, item);
            break;
        case OptionControl::Choice:
            OpenOptionChoice(app, item);
            break;
        case OptionControl::Stepper:
            item.valueText = std::to_string(atoi(item.valueText.c_str()) +
                                            (x < control.x + control.w * 0.5f ? -1 : 1));
            ApplyOptionFromItem(app, item);
            break;
        case OptionControl::Slider:
        {
            const float knob  = control.h * 0.5f;
            const float track = (std::max)(1.0f, control.w - knob * 2.0f);
            float fraction = (x - (control.x + knob)) / track;
            fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
            item.fraction = fraction;
            app.optionSliderDrag = item.id;   // il trascinamento continua nel movimento
            ApplyOptionFromItem(app, item);
            break;
        }
        default:
            break;
    }
    return MenuAction::None;
}

void RunCommand(AppState& app, int command)
{
    const int cursorIndex = CursorIndex(app);

    switch (static_cast<CommandId>(command))
    {
        case CmdNewTab:  NewTab(app, app.directory.empty() ? L"C:\\" : app.directory); break;
        case CmdCloseTab: CloseTab(app, app.activeTab); break;
        case CmdNextTab:
            if (app.tabs.size() > 1) ActivateTab(app, (app.activeTab + 1) % static_cast<int>(app.tabs.size()));
            break;
        case CmdPrevTab:
            if (app.tabs.size() > 1)
            {
                const int count = static_cast<int>(app.tabs.size());
                ActivateTab(app, (app.activeTab + count - 1) % count);
            }
            break;

        // In una cartella dello spazio dei nomi il genitore lo sa la shell: sopra
        // il cestino c'e' il desktop, e `ParentOf` su "::{...}" non troverebbe
        // niente da tagliare.
        case CmdGoUp:      LoadDirectory(app, ShellLocation::IsNamespace(app.directory)
                                                ? ShellLocation::ParentOf(app.directory)
                                                : ParentOf(app.directory)); break;
        case CmdBack:      NavigateBack(app); break;
        case CmdForward:   NavigateForward(app); break;
        case CmdGoTo:      BuildGoToMenu(app); break;
        case CmdRefresh:   app.stats.ClearAll(); LoadDirectory(app, app.directory); break;
        case CmdRecalcStats: app.stats.ClearAll(); break;

        case CmdCopy:      DoCopyToClipboard(app, /*cut=*/false); break;
        case CmdCut:       DoCopyToClipboard(app, /*cut=*/true); break;
        case CmdPaste:     DoPaste(app); break;
        case CmdRename:    BeginRename(app); break;
        case CmdNewFolder: DoNewFolder(app); break;
        case CmdDelete:          DoDelete(app, /*permanently=*/false); break;
        case CmdDeletePermanent: DoDelete(app, /*permanently=*/true); break;

        case CmdOpenExplorer:
            // Dentro una cartella dello spazio dei nomi `/select` non vale, e non
            // serve: si apre la cartella stessa, che e' quello che si vede.
            if (ShellLocation::IsNamespace(app.directory))
            {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", app.directory.c_str(), nullptr,
                              SW_SHOWNORMAL);
            }
            else if (cursorIndex >= 0)
            {
                const std::wstring full = JoinPath(app.directory, app.entries[static_cast<size_t>(cursorIndex)].wideName);
                const std::wstring arguments = L"/select,\"" + full + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
            }
            else
            {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", app.directory.c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;

        case CmdOpenConsole:
            ShellExecuteW(nullptr, L"open", L"cmd.exe", nullptr, app.directory.c_str(), SW_SHOWNORMAL);
            break;

        case CmdToggleSidebar:
            app.sidebarVisible = !app.sidebarVisible;
            RememberSettings(app);
            break;
        // Una preferenza si cambia e si **ricorda**: e' quello che l'autore ha
        // chiesto per la lingua, e vale per tutte quelle che si scelgono qui.
        case CmdToggleHoverToSelect:    app.hoverToSelect    = !app.hoverToSelect;    RememberSettings(app); break;
        case CmdToggleUnderlineOnHover: app.underlineOnHover = !app.underlineOnHover; RememberSettings(app); break;
        case CmdToggleTooltips:         app.showTooltips     = !app.showTooltips;     RememberSettings(app); break;
        case CmdSingleClick:            app.singleClick            = !app.singleClick;            RememberSettings(app); break;
        case CmdSingleClickFoldersOnly: app.singleClickFoldersOnly = !app.singleClickFoldersOnly; RememberSettings(app); break;
        case CmdActOnPress:             app.actOnPress             = !app.actOnPress;             RememberSettings(app); break;
        case CmdDoubleClickToParent:    app.doubleClickToParent    = !app.doubleClickToParent;    RememberSettings(app); break;
        case CmdShowCheckboxes:         app.showCheckboxes         = !app.showCheckboxes;         RememberSettings(app); break;
        case CmdLanguageItalian:
            gLanguage = Language::Italian;
            RememberSettings(app);
            ReportStatus(app, L"Impostazioni salvate in " + SettingsPath());
            break;
        case CmdLanguageEnglish:
            gLanguage = Language::English;
            RememberSettings(app);
            ReportStatus(app, L"Impostazioni salvate in " + SettingsPath());
            break;
        case CmdShowSettingsPath:
            ReportStatus(app, SettingsPath().empty()
                                 ? TrW(L"Impostazioni non disponibili", L"Settings unavailable")
                                 : TrW(L"Impostazioni in ", L"Settings in ") + SettingsPath());
            break;
        case CmdOpenOptions:
            BuildOptionsMenu(app);
            break;
        case CmdCloseInTree:
            CloseTabTree(app, app.tabs[static_cast<size_t>(app.activeTab)].id, /*wholeTree=*/false);
            break;
        case CmdCloseAllInTree:
            CloseTabTree(app, app.tabs[static_cast<size_t>(app.activeTab)].id, /*wholeTree=*/true);
            break;
        case CmdDetachTab:
        {
            // "Stacca" riporta il tab alla radice: e' l'unico modo di disfare un
            // annidamento senza cercarne un altro con il mouse.
            Tab& tab = app.tabs[static_cast<size_t>(app.activeTab)];
            tab.parentId = -1;
            break;
        }
        case CmdZoomIn:    app.zoom = (std::min)(2.0f, app.zoom + 0.1f); break;

        // Le quattro opzioni del visualizzatore. Cambiano il disegno del testo, non
        // la lista: la riga da rifare e' quella delle righe visibili, e si rifa' da
        // sola perche' il calcolo si accorge che l'andare a capo e' cambiato.
        case CmdTextWhitespace:  app.textShowWhitespace = !app.textShowWhitespace; break;
        case CmdTextLineNumbers: app.textLineNumbers = !app.textLineNumbers; break;
        case CmdTextMinimap:     app.textMinimap = !app.textMinimap; break;
        case CmdTextWrap:
            app.textWrap = !app.textWrap;
            app.textRowsDirty = true;
            break;
        case CmdTextZoomIn:
            app.textZoom = (std::min)(2.4f, app.textZoom + 0.15f);
            app.textRowsDirty = true;
            break;
        case CmdTextZoomOut:
            app.textZoom = (std::max)(0.6f, app.textZoom - 0.15f);
            app.textRowsDirty = true;
            break;

        // L'ingrandimento dell'anteprima e' **relativo all'adattamento**: il
        // decoder la ridimensiona a 1024 px al massimo, quindi "1:1 rispetto
        // all'originale" non e' una cosa che si possa promettere.
        case CmdImageZoomIn:  app.imageZoom = (std::min)(4.0f, app.imageZoom + 0.25f); break;
        case CmdImageZoomOut: app.imageZoom = (std::max)(0.25f, app.imageZoom - 0.25f); break;
        case CmdImageZoomFit: app.imageZoom = 1.0f; break;
        case CmdZoomOut:   app.zoom = (std::max)(0.6f, app.zoom - 0.1f); break;
        case CmdZoomReset: app.zoom = app.dpiScale; break;
        case CmdToggleDetails: app.palette.detailed = !app.palette.detailed; break;
        // Cambiando vista le celle si spostano, e l'indice della riga sotto il
        // puntatore non vuol piu' dire niente: il sovrappasso si azzera e lo
        // ricalcola il prossimo movimento. Senza, il suggerimento resta appeso a
        // una voce che non e' piu' quella sotto il mouse.
        case CmdViewDetails: SetViewMode(app, ViewMode::Details); break;
        case CmdViewList:    SetViewMode(app, ViewMode::List);    break;
        case CmdViewTiles:   SetViewMode(app, ViewMode::Tiles);   break;
        case CmdViewIcons:   SetViewMode(app, ViewMode::Icons);   break;
        case CmdToggleBookmark: ToggleBookmark(app); break;
        case CmdClearBookmarks: app.bookmarks.clear(); break;
        case CmdClearRecents:   app.recents.clear(); break;

        case CmdSortName:     app.sortColumn = SortColumn::Name;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortType:     app.sortColumn = SortColumn::Type;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortModified: app.sortColumn = SortColumn::Modified; SortEntries(app); app.viewDirty = true; break;
        case CmdSortFolders:  app.sortColumn = SortColumn::Folders;  SortEntries(app); app.viewDirty = true; break;
        case CmdSortFiles:    app.sortColumn = SortColumn::Files;    SortEntries(app); app.viewDirty = true; break;
        case CmdSortSize:     app.sortColumn = SortColumn::Size;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortDirection: app.sortAscending = !app.sortAscending; SortEntries(app); app.viewDirty = true; break;

        case CmdGroupNone:   app.groupMode = GroupMode::None;   app.viewDirty = true; break;
        case CmdGroupName:   app.groupMode = GroupMode::Name;   app.viewDirty = true; break;
        case CmdGroupDate:   app.groupMode = GroupMode::Date;   app.viewDirty = true; break;
        case CmdGroupSize:   app.groupMode = GroupMode::Size;   app.viewDirty = true; break;
        case CmdGroupCount:  app.groupMode = GroupMode::Count;  app.viewDirty = true; break;
        case CmdGroupVolume: app.groupMode = GroupMode::Volume; app.viewDirty = true; break;

        case CmdFocusFilter: app.filterActive = true; break;
        case CmdClearFilter:
            app.filter.clear();
            app.filterActive = false;
            app.viewDirty = true;
            break;

        case CmdScrollTop:
            app.scroll = 0.0f;
            break;
        case CmdScrollBottom:
        {
            const Layout layout = ComputeLayout(app);
            const float content = static_cast<float>(app.rows.size()) * layout.rowHeight;
            app.scroll = (std::max)(0.0f, content - layout.list.h);
            break;
        }
    }
}

Rect GroupMenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu)
{
    const float scale = layout.scale;
    // Il pannello delle opzioni ha bisogno di piu' larghezza: una descrizione
    // accanto a un controllo, dentro 560 px, non ci sta.
    const float maxWidth = menu.richRows ? 760.0f : 560.0f;
    const float width  = (std::min)(maxWidth * scale, static_cast<float>(app.viewportWidth) - 60.0f * scale);
    const float height = (std::min)((menu.richRows ? 560.0f : 420.0f) * scale,
                                    static_cast<float>(app.viewportHeight) - 90.0f * scale);
    return Rect{(static_cast<float>(app.viewportWidth) - width) * 0.5f, 70.0f * scale, width, height};
}

// Il pannello dei valori di una colonna: piu' stretto degli altri, appoggiato sotto
// l'intestazione e alto quanto basta ai suoi valori, invece di 420 px di vuoto.
Rect ColumnMenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu)
{
    const float scale = layout.scale;
    const float width = 260.0f * scale;
    const float wanted = 84.0f * scale + static_cast<float>(menu.rows.size()) * layout.rowHeight;
    const float room = static_cast<float>(app.viewportHeight) - menu.anchor.bottom() - 20.0f * scale;
    const float height = (std::min)((std::min)(520.0f * scale, wanted), (std::max)(160.0f * scale, room));
    float x = menu.anchor.x;
    if (x + width > static_cast<float>(app.viewportWidth) - 10.0f * scale)
        x = static_cast<float>(app.viewportWidth) - width - 10.0f * scale;
    return Rect{x, menu.anchor.bottom() + 2.0f * scale, width, height};
}

Rect MenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu)
{
    return menu.anchored ? ColumnMenuPanel(app, layout, menu) : GroupMenuPanel(app, layout, menu);
}

// --- menu contestuale: geometria, disegno, interazione ----------------------
void ActivateSelection(AppState& app);

// Un clic solo attiva? Dipende da due chiavi, e la regola sta in un posto solo
// perche' i tre gesti che la usano (pressione, rilascio, doppio clic) non possano
// rispondere in modo diverso.
bool SingleClickActivates(const AppState& app, const Entry& entry)
{
    if (!app.singleClick)
        return false;
    if (app.singleClickFoldersOnly && !entry.isDirectory)
        return false;   // sui file serve ancora il doppio clic
    return true;
}

// --- i livelli del menu contestuale ----------------------------------------
//
// Un menu non e' una lista: e' un albero di liste. Lo chiamiamo *livello*: 0 e'
// il primo, 1 il sottomenu aperto, e cosi' via. `openPath` tiene gli indici delle
// voci aperte, uno per livello, e `levels` la scelta e la finestra di ogni
// livello. Il livello piu' profondo aperto e' quello su cui si agisce.
const std::vector<ContextMenuItem>* LevelItems(const ContextMenu& menu, int depth)
{
    const std::vector<ContextMenuItem>* items = &menu.items;
    for (int i = 0; i < depth; ++i)
    {
        if (i >= static_cast<int>(menu.openPath.size()))
            return nullptr;
        const int index = menu.openPath[static_cast<size_t>(i)];
        if (index < 0 || index >= static_cast<int>(items->size()))
            return nullptr;
        items = &(*items)[static_cast<size_t>(index)].children;
    }
    return items;
}

int LevelCount(const ContextMenu& menu, int depth)
{
    const std::vector<ContextMenuItem>* items = LevelItems(menu, depth);
    return items == nullptr ? 0 : static_cast<int>(items->size());
}

const ContextMenuItem* LevelItem(const ContextMenu& menu, int depth, int index)
{
    const std::vector<ContextMenuItem>* items = LevelItems(menu, depth);
    if (items == nullptr || index < 0 || index >= static_cast<int>(items->size()))
        return nullptr;
    return &(*items)[static_cast<size_t>(index)];
}

// Come LevelItem, ma per chi deve SCRIVERE nella voce: il contenuto di un
// sottomenu della shell si copia dentro `children` alla prima apertura.
ContextMenuItem* LevelItemMutable(ContextMenu& menu, int depth, int index)
{
    std::vector<ContextMenuItem>* items = &menu.items;
    for (int i = 0; i < depth; ++i)
    {
        if (i >= static_cast<int>(menu.openPath.size()))
            return nullptr;
        const int parent = menu.openPath[static_cast<size_t>(i)];
        if (parent < 0 || parent >= static_cast<int>(items->size()))
            return nullptr;
        items = &(*items)[static_cast<size_t>(parent)].children;
    }
    if (index < 0 || index >= static_cast<int>(items->size()))
        return nullptr;
    return &(*items)[static_cast<size_t>(index)];
}

// La voce ha qualcosa dentro? Vale anche per un sottomenu della shell **non
// ancora letto**: il suo contenuto non esiste prima di aprirlo, e chiederlo per
// tutti a ogni tasto destro costerebbe quanto interrogare la shell dieci volte.
bool HasSubmenu(const ContextMenuItem& item)
{
    return item.shellSubmenu != nullptr || !item.children.empty() || item.lazyNewFile ||
           item.lazySendTo;
}

int ActiveDepth(const ContextMenu& menu)
{
    return static_cast<int>(menu.openPath.size());
}

float ContextMenuWidth(const AppState& app, const Layout& layout, int depth)
{
    const float scale = layout.scale;
    float widest = 0.0f;
    float arrow  = 0.0f;
    const int total = LevelCount(app.contextMenu, depth);
    for (int i = 0; i < total; ++i)
    {
        const ContextMenuItem* item = LevelItem(app.contextMenu, depth, i);
        widest = (std::max)(widest, app.font.Measure(item->label, scale));
        if (HasSubmenu(*item))
            arrow = 18.0f * scale;   // lo spazio del segno di sottomenu
    }

    // La larghezza si DERIVA dal contenuto, con un minimo e un massimo: fissa
    // taglierebbe le voci lunghe come "Apri in Esplora risorse", e larga quanto
    // l'etichetta piu' lunga lascerebbe il sottotitolo a sbattere sul bordo.
    return (std::max)(220.0f * scale, (std::min)(340.0f * scale, widest + 56.0f * scale + arrow));
}

// Quante voci entrano nell'altezza utile, a partire da |firstItem|.
//
// Il taglio cade sempre fra due voci. Una voce mostrata a meta' si sceglie per
// sbaglio, e con i verbi della shell il menu supera la finestra molto prima di
// quanto si creda: venti voci su un monitor normale sono piu' della finestra.
int ContextMenuVisibleCount(const AppState& app, const Layout& layout, int depth, int firstItem)
{
    const float scale     = layout.scale;
    const float separator = 7.0f * scale;
    // 8 px di margine sotto, 6 px di riempimento sopra e sotto: quello che resta
    // e' lo spazio delle voci. Il sottotitolo ce l'ha solo il primo livello.
    const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
    const float room = static_cast<float>(app.viewportHeight) - 8.0f * scale -
                       12.0f * scale - footer;

    const int total = LevelCount(app.contextMenu, depth);
    float used  = 0.0f;
    int   count = 0;
    for (int i = firstItem; i < total; ++i)
    {
        const ContextMenuItem* item = LevelItem(app.contextMenu, depth, i);
        const float height = (item != nullptr && item->separator) ? separator : layout.rowHeight;
        // Almeno una voce si mostra sempre: un menu vuoto non si spiega.
        if (count > 0 && used + height > room)
            break;
        used += height;
        ++count;
    }
    return count;
}

// I due si chiamano a vicenda: il pannello di un sottomenu si ancora alla riga
// della voce che lo apre, e la riga si misura dentro il pannello.
Rect ContextMenuRowRect(const AppState& app, const Layout& layout, int depth, int index);

Rect ContextMenuPanel(const AppState& app, const Layout& layout, int depth)
{
    const float scale     = layout.scale;
    const float separator = 7.0f * scale;
    const float padding   = 6.0f * scale;

    const ContextMenu& menu = app.contextMenu;
    const int first   = menu.levels[static_cast<size_t>(depth)].firstItem;
    const int visible = ContextMenuVisibleCount(app, layout, depth, first);

    float content = 0.0f;
    for (int i = first; i < first + visible; ++i)
    {
        const ContextMenuItem* item = LevelItem(menu, depth, i);
        content += (item != nullptr && item->separator) ? separator : layout.rowHeight;
    }

    const float width = ContextMenuWidth(app, layout, depth);
    // 22 px in fondo per il sottotitolo, che c'e' solo nel primo livello.
    const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
    const float height = content + padding * 2.0f + footer;

    // L'ancora: il primo livello sta dove ha cliccato il puntatore, un sottomenu
    // accanto alla voce che lo apre.
    float anchorX = menu.x;
    float anchorY = menu.y;
    if (depth > 0)
    {
        const Rect parent = ContextMenuPanel(app, layout, depth - 1);
        const int parentIndex = menu.openPath[static_cast<size_t>(depth - 1)];
        const Rect row = ContextMenuRowRect(app, layout, depth - 1, parentIndex);
        anchorX = parent.right() - 4.0f * scale;
        anchorY = row.y - padding;

        // A destra non ci sta? Si apre a sinistra, come i menu di Windows: un
        // sottomenu che esce dalla finestra non si puo' leggere.
        if (anchorX + width > static_cast<float>(app.viewportWidth) - 4.0f * scale)
            anchorX = parent.x - width + 4.0f * scale;
    }

    // Il menu si ancora al puntatore ma non esce dalla finestra: aperto accanto
    // al bordo destro finirebbe per meta' fuori e irraggiungibile.
    const float maxX = static_cast<float>(app.viewportWidth) - width - 4.0f * scale;
    const float maxY = static_cast<float>(app.viewportHeight) - height - 4.0f * scale;
    const float x = (std::max)(4.0f * scale, (std::min)(anchorX, maxX));
    const float y = (std::max)(4.0f * scale, (std::min)(anchorY, maxY));
    return Rect{x, y, width, height};
}

// Ultimo indice disegnato, escluso. Fuori da questo intervallo una voce non
// esiste: non si disegna, non si sovrappassa e non si clicca. Cosi' non serve
// nessun taglio, perche' quello che non si disegna non sborda.
int ContextMenuLastVisible(const AppState& app, const Layout& layout, int depth)
{
    const int first   = app.contextMenu.levels[static_cast<size_t>(depth)].firstItem;
    const int visible = ContextMenuVisibleCount(app, layout, depth, first);
    return (std::min)(LevelCount(app.contextMenu, depth), first + visible);
}

Rect ContextMenuRowRect(const AppState& app, const Layout& layout, int depth, int index)
{
    const Rect panel       = ContextMenuPanel(app, layout, depth);
    const float scale      = layout.scale;
    const float separator  = 7.0f * scale;
    const float padding    = 6.0f * scale;

    const ContextMenu& menu = app.contextMenu;
    float top = panel.y + padding;
    for (int i = menu.levels[static_cast<size_t>(depth)].firstItem; i < index; ++i)
    {
        const ContextMenuItem* item = LevelItem(menu, depth, i);
        top += (item != nullptr && item->separator) ? separator : layout.rowHeight;
    }

    const ContextMenuItem* item = LevelItem(menu, depth, index);
    const float height = (item != nullptr && item->separator) ? separator : layout.rowHeight;
    return Rect{panel.x + 4.0f * scale, top, panel.w - 8.0f * scale, height};
}

// Tiene la finestra del menu dentro i limiti e ci tiene dentro la voce scelta:
// l'Invio esegue quello che si vede, quindi la selezione non deve poter uscire
// dal pannello.
void ContextMenuClampWindow(AppState& app, const Layout& layout, int depth,
                            bool followSelection = true)
{
    ContextMenu& menu = app.contextMenu;
    if (depth < 0 || depth >= static_cast<int>(menu.levels.size()))
        return;

    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int total = LevelCount(menu, depth);
    if (total == 0)
    {
        level.firstItem = 0;
        return;
    }

    // La rotella scorre la finestra SENZA trascinarsi dietro la scelta: la voce
    // sotto il puntatore e' scelta dal sovrappasso, e obbligarla a restare
    // visibile faceva resistire il menu alla rotella stessa. Per le frecce
    // l'opposto e' vero, e la voce scelta non deve poter uscire dal pannello.
    if (!followSelection)
    {
        const int capacity = (std::max)(1, ContextMenuVisibleCount(app, layout, depth, 0));
        level.firstItem =
            (std::max)(0, (std::min)(level.firstItem, (std::max)(0, total - capacity)));

        if (level.selected >= 0 &&
            (level.selected < level.firstItem ||
             level.selected >= ContextMenuLastVisible(app, layout, depth)))
            level.selected = -1;   // l'Invio esegue quello che si vede, o niente
        return;
    }

    const int capacity = (std::max)(1, ContextMenuVisibleCount(app, layout, depth, 0));
    level.firstItem = (std::max)(0, (std::min)(level.firstItem, (std::max)(0, total - capacity)));

    if (level.selected < 0)
        return;
    if (level.selected < level.firstItem)
    {
        level.firstItem = level.selected;
        return;
    }

    // Si avanza finche' la voce scelta non entra: quante ne entrano dipende da
    // dove si comincia, perche' un separatore non e' alto quanto una voce.
    while (level.firstItem < level.selected &&
           level.firstItem + ContextMenuVisibleCount(app, layout, depth, level.firstItem) <=
               level.selected)
        ++level.firstItem;
    level.firstItem = (std::min)(level.firstItem, level.selected);
}

// --- verbi della shell nel menu contestuale ---------------------------------
//
// La shell sa fare cose che non sono nostre: "Apri con", "Invia a", i verbi
// delle estensioni installate. Non si reimplementa niente: le si chiede
// l'oggetto di interfaccia della selezione, le si fa descrivere il proprio menu
// dentro un HMENU che non mostriamo mai, se ne leggono le voci e se ne esegue una
// quando l'utente la sceglie. Il binario di File Pilot fa esattamente questo —
// `Win_ContextMenuBuild`, `P_GetContextMenu`, `P_ContextMenuInvoke`, e la
// stringa `GCS_VERBW` per leggere i verbi.

// Gli id della shell vivono in una banda riservata: dentro quella banda un id e'
// un indice per IContextMenu, fuori e' un nostro CommandId.
constexpr int kShellCmdFirst = 0x6000;
constexpr int kShellCmdLast  = 0x6FFF;

// Le voci di "Nuovo file" hanno una banda loro, per lo stesso motivo: l'indice e'
// la posizione del modello nell'elenco letto dal registro.
constexpr int kNewItemCmdFirst = 0x7000;
constexpr int kNewItemCmdLast  = 0x70FF;

// Le destinazioni di "Invia a": stessa banda per lo stesso motivo, l'indice e' la
// posizione nell'elenco letto dalla cartella SendTo.
constexpr int kSendToCmdFirst = 0x7100;
constexpr int kSendToCmdLast  = 0x71FF;


// L'atlante dei glifi copre il solo ASCII stampabile, quindi un'etichetta della
// shell con un accento mostrerebbe un buco: si ripiega sull'ASCII, come fa gia'
// "Proprieta'" scritto a mano.
char FoldAccent(wchar_t c)
{
    switch (c)
    {
        case L'\u00e0': case L'\u00e1': case L'\u00e2': case L'\u00e3': case L'\u00e4': case L'\u00e5': return 'a';
        case L'\u00c0': case L'\u00c1': case L'\u00c2': case L'\u00c3': case L'\u00c4': case L'\u00c5': return 'A';
        case L'\u00e7': return 'c';
        case L'\u00c7': return 'C';
        case L'\u00e8': case L'\u00e9': case L'\u00ea': case L'\u00eb': return 'e';
        case L'\u00c8': case L'\u00c9': case L'\u00ca': case L'\u00cb': return 'E';
        case L'\u00ec': case L'\u00ed': case L'\u00ee': case L'\u00ef': return 'i';
        case L'\u00cc': case L'\u00cd': case L'\u00ce': case L'\u00cf': return 'I';
        case L'\u00f1': return 'n';
        case L'\u00d1': return 'N';
        case L'\u00f2': case L'\u00f3': case L'\u00f4': case L'\u00f5': case L'\u00f6': return 'o';
        case L'\u00d2': case L'\u00d3': case L'\u00d4': case L'\u00d5': case L'\u00d6': return 'O';
        case L'\u00f9': case L'\u00fa': case L'\u00fb': case L'\u00fc': return 'u';
        case L'\u00d9': case L'\u00da': case L'\u00db': case L'\u00dc': return 'U';
        case L'\u00fd': case L'\u00ff': return 'y';
        default: break;
    }
    return '\0';
}

std::string ShellLabelToAscii(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];

        // Nelle etichette della shell la e commerciale segnala il tasto di scelta
        // rapida e si raddoppia per scriverla davvero: "Aggiungi ad &&Accesso"
        // significa "Aggiungi ad &Accesso". Il nostro menu non ha tasti di scelta
        // rapida, quindi l'occhiello si toglie invece di finire a schermo.
        if (c == L'&')
        {
            if (i + 1 < text.size() && text[i + 1] == L'&')
            {
                out.push_back('&');
                ++i;
            }
            continue;
        }

        if (c >= 32 && c < 127)
        {
            out.push_back(static_cast<char>(c));
            continue;
        }

        // Gli accenti si passano come sono: l'atlante li disegna, adesso. Prima si
        // ripiegavano su ASCII, e "Citta'" era l'unica forma scrivibile — anche per
        // una cartella che si chiama "Città".
        if (c >= 0x00A0 && c <= 0x00FF)
        {
            out += ToUtf8(std::wstring(1, c));
            continue;
        }

        // Oltre il latino-1 resta il ripiego: meglio una lettera senza segno che un
        // buco. Quello che non si sa ripiegare si passa lo stesso, e sara' l'atlante
        // a non avere il glifo — che e' diverso dal perdere il carattere qui.
        const char folded = FoldAccent(c);
        if (folded != '\0')
        {
            out.push_back(folded);
            continue;
        }
        out += ToUtf8(std::wstring(1, c));
    }
    return out;
}

bool SameWord(const wchar_t* a, const wchar_t* b)
{
    while (*a != L'\0' && *b != L'\0')
    {
        if (towlower(*a) != towlower(*b))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

// I verbi che il nostro menu gia' offre: ripeterli accanto ai nostri comandi
// darebbe due "Taglia" e due "Proprieta'" nella stessa lista. Il confronto e' per
// verbo canonico e non per etichetta, perche' l'etichetta e' tradotta e il verbo
// no. "Apri con" (openas) NON e' fra questi: e' proprio uno dei motivi per cui
// la shell viene interrogata.
//
// Sul menu della CARTELLA l'elenco e' piu' corto, e non per svogliatezza: la'
// il nostro menu non ha ne' "Apri" ne' "Proprieta'", quindi quelli della shell
// sono un guadagno e non un doppione. Resta fuori solo cio' che c'e' in tutti e
// due, cioe' l'incolla.
bool IsOwnShellVerb(const wchar_t* verb, bool folderBackground)
{
    if (folderBackground)
        return SameWord(verb, L"paste");

    static const wchar_t* const kOwn[] = {
        L"open", L"cut", L"copy", L"paste", L"delete", L"rename", L"properties",
    };
    for (const wchar_t* own : kOwn)
    {
        if (SameWord(verb, own))
            return true;
    }
    return false;
}

// Rilascia quello che la shell ci ha prestato. Va chiamato a ogni chiusura del
// menu: un IContextMenu tenuto in vita e' una DLL di estensione che non si
// scarica, e un HMENU mai distrutto e' una risorsa utente che cola.
// Il menu **lascia andare** gli oggetti della shell: non li distrugge, perche' non
// sono suoi. Il proprietario e' la memoria dei verbi, che li rilascia quando il
// risultato non serve piu' a nessuno (`ReleaseShellMenuCache`).
void ShellMenuDetach(ContextMenu& menu)
{
    menu.shell     = nullptr;
    menu.shellMenu = nullptr;
}

// Rilascia davvero. Si chiama quando la selezione cambia, quando la memoria scade,
// e all'uscita.
void ReleaseShellMenuCache(AppState& app)
{
    AppState::ShellMenuCache& cache = app.shellMenuCache;
    if (cache.shell != nullptr)
    {
        cache.shell->Release();
        cache.shell = nullptr;
    }
    if (cache.handle != nullptr)
    {
        DestroyMenu(cache.handle);
        cache.handle = nullptr;
    }
    cache.valid = false;
    cache.items.clear();
    cache.key.clear();
}

// I sottomenu che la shell popola **da sé** — "Nuovo", "Invia a", "Dare accesso
// a" — e che da qui non si possono eseguire: la shell consegna i comandi di
// quei sottomenu alla finestra del menu, e noi una finestra di menu non
// l'abbiamo. Offrirli sarebbe offrire voci che non fanno niente.
//
// Il confronto è per verbo canonico. Una voce senza verbo viene trattata come
// della shell: non si può sapere di chi è, e la prudenza costa una voce, non un
// comando che finge di riuscire.
bool IsShellOwnSubmenuVerb(const wchar_t* verb)
{
    if (verb[0] == L'\0')
        return true;

    static const wchar_t* const kShellOwn[] = {
        L"New",      L"Windows.Share", L"Windows.SendTo", L"SendTo",
        L"View",     L"Arrange",       L"Arrangement",    L"Sort",
        L"Group",    L"Layout",        L"Background",     L"Customize",
    };
    for (const wchar_t* own : kShellOwn)
    {
        if (SameWord(verb, own))
            return true;
    }
    return false;
}

// Legge le voci di un menu della shell — separatori compresi — e le traduce nel
// nostro elenco. Vale per il menu della selezione, per quello del fondo della
// cartella, e un livello alla volta per un sottomenu.
void ReadShellMenu(IContextMenu* shell, HMENU handle, bool folderBackground,
                   std::vector<ContextMenuItem>* out, int* outSkipped)
{
        const int count = GetMenuItemCount(handle);
        for (int i = 0; i < count; ++i)
        {
            MENUITEMINFOW basic{};
            basic.cbSize = sizeof(basic);
            basic.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(handle, static_cast<UINT>(i), TRUE, &basic))
                continue;

            if ((basic.fType & MFT_SEPARATOR) != 0)
            {
                out->push_back({"", 0, /*separator=*/true});
                continue;
            }

            // Fuori banda la voce non si potrebbe eseguire: meglio non offrirla
            // che offrirla e non farla partire.
            if (basic.wID < kShellCmdFirst || basic.wID >= kShellCmdLast)
                continue;

            const UINT offset = static_cast<UINT>(basic.wID) - kShellCmdFirst;

            wchar_t verb[128]{};
            const bool haveVerb = SUCCEEDED(shell->GetCommandString(
                offset, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), 128));

            if (haveVerb && IsOwnShellVerb(verb, folderBackground))
                continue;

            wchar_t text[256]{};
            MENUITEMINFOW detailed{};
            detailed.cbSize     = sizeof(detailed);
            detailed.fMask      = MIIM_STRING;
            detailed.dwTypeData = text;
            detailed.cch        = 255;
            GetMenuItemInfoW(handle, static_cast<UINT>(i), TRUE, &detailed);

            ContextMenuItem item;
            item.label    = ShellLabelToAscii(text);
            item.command  = static_cast<int>(basic.wID);
            item.disabled = (basic.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;

            // Un sottomenu si legge **quando si apre**, non adesso: il suo
            // contenuto non esiste prima di WM_INITMENUPOPUP, e chiederlo per
            // tutti a ogni tasto destro costerebbe quanto interrogare la shell
            // dieci volte, per voci che magari nessuno apre. `shellPosition` e'
            // dove la voce sta nel menu padre, e la shell vuole saperlo per
            // decidere cosa popolare.
            if (basic.hSubMenu != nullptr)
            {
                // Che sia di un'estensione o della shell decide se la voce si
                // potra' eseguire: vedi IsShellOwnSubmenuVerb.
                if (!haveVerb || IsShellOwnSubmenuVerb(verb))
                {
                    DiagLog("  saltata (sottomenu della shell, non eseguibile): '" +
                            item.label + "'");
                    ++(*outSkipped);
                    continue;
                }
                item.command       = 0;
                item.shellSubmenu  = basic.hSubMenu;
                item.shellPosition = i;
            }

            // Le voci disegnate da chi le ha messe (ownerdraw) non portano il
            // testo nel menu: il ripiego e' il testo di aiuto, che e' meglio del
            // nome tecnico del verbo.
            if (item.label.empty())
            {
                wchar_t help[256]{};
                if (SUCCEEDED(shell->GetCommandString(offset, GCS_HELPTEXTW, nullptr,
                                                      reinterpret_cast<LPSTR>(help), 256)))
                    item.label = ShellLabelToAscii(help);
            }
            if (item.label.empty())
            {
                DiagLog("  saltata (senza etichetta): verbo '" + ShellLabelToAscii(verb) + "'");
                ++(*outSkipped);
                continue;
            }

            DiagLog("  voce: '" + item.label + "'" + (haveVerb ? " verbo '" : " verbo '?") +
                    ShellLabelToAscii(verb) + "'" + (item.disabled ? " (spenta)" : ""));
            out->push_back(std::move(item));
        }

        // La shell apre e chiude con dei separatori suoi, che accanto ai nostri
        // diventerebbero doppi; e togliendo le voci che offriamo gia' possono
        // restarne due di fila.
        std::vector<ContextMenuItem> cleaned;
        for (ContextMenuItem& item : *out)
        {
            if (item.separator && (cleaned.empty() || cleaned.back().separator))
                continue;
            cleaned.push_back(std::move(item));
        }
        while (!cleaned.empty() && cleaned.back().separator)
            cleaned.pop_back();
        *out = std::move(cleaned);
}

// Chiede alla shell il menu della selezione e ne copia le voci utilizzabili.
// L'IContextMenu esce da qui perche' serve ancora al momento dell'esecuzione: la
// shell non accetta un verbo senza l'oggetto da cui e' uscito.
bool BuildShellItems(HWND owner, const std::vector<std::wstring>& paths,
                     const std::vector<const BYTE*>& selectionPidls, bool folderBackground,
                     IContextMenu** outMenu, HMENU* outHandle,
                     std::vector<ContextMenuItem>* out, int* outSkipped)
{
    *outMenu    = nullptr;
    *outHandle  = nullptr;
    *outSkipped = 0;
    if (paths.empty())
        return false;

    std::vector<PIDLIST_ABSOLUTE> pidls;
    IShellFolder* parent = nullptr;
    IContextMenu* shell   = nullptr;

    // Il menu del FONDO della cartella non e' il menu della cartella come
    // elemento, e la shell li tiene distinti. La differenza si vede: il secondo
    // offre Taglia, Copia ed Elimina **sulla cartella stessa**, che a destra di
    // un fondo non se li aspetta nessuno. Il fondo si chiede alla vista, con
    // `CreateViewObject`; la cartella come elemento con `GetUIObjectOf`. Chiedere
    // quello sbagliato non da' un errore: da' un menu plausibile e pericoloso.
    if (folderBackground)
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(paths.front().c_str(), nullptr, &pidl, 0, nullptr)))
        {
            IShellFolder* folder = nullptr;
            if (SUCCEEDED(SHBindToObject(nullptr, pidl, nullptr, IID_IShellFolder,
                                         reinterpret_cast<void**>(&folder))))
            {
                folder->CreateViewObject(owner, IID_IContextMenu,
                                         reinterpret_cast<void**>(&shell));
                folder->Release();
            }
            CoTaskMemFree(pidl);
        }
    }
    else if (!selectionPidls.empty())
    {
        // Le voci selezionate **hanno un PIDL**: si usa quello, e non il loro nome.
        // Per una voce di spazio dei nomi il nome e' un'altra cosa — il percorso del
        // file dentro `$Recycle.Bin` — e la shell, se le si chiede il menu di quel
        // nome, risponde col menu di quel file: "Ripristina" non c'e', e al suo
        // posto ci sono i verbi del tipo di file. Sembra ragionevole ed e' sbagliato.
        PCUIDLIST_ABSOLUTE first = reinterpret_cast<PCUIDLIST_ABSOLUTE>(selectionPidls.front());
        if (SUCCEEDED(SHBindToParent(first, IID_IShellFolder, reinterpret_cast<void**>(&parent),
                                     nullptr)) &&
            parent != nullptr)
        {
            std::vector<PCUITEMID_CHILD> children;
            children.reserve(selectionPidls.size());
            for (const BYTE* pidl : selectionPidls)
                children.push_back(ILFindLastID(reinterpret_cast<PCUIDLIST_ABSOLUTE>(pidl)));

            parent->GetUIObjectOf(owner, static_cast<UINT>(children.size()), children.data(),
                                  IID_IContextMenu, nullptr, reinterpret_cast<void**>(&shell));
        }
    }
    else
    {
        for (const std::wstring& path : paths)
        {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)))
                break;

            // La lista mostra una cartella sola, quindi il padre di tutti i percorsi
            // selezionati e' lo stesso: quello del primo.
            if (parent == nullptr &&
                FAILED(SHBindToParent(pidl, IID_IShellFolder, reinterpret_cast<void**>(&parent), nullptr)))
            {
                CoTaskMemFree(pidl);
                break;
            }
            pidls.push_back(pidl);
        }

        if (parent != nullptr && !pidls.empty())
        {
            // GetUIObjectOf vuole i figli relativi alla cartella padre, e l'ultimo id
            // di un pidl assoluto e' esattamente quello.
            std::vector<PCUITEMID_CHILD> children;
            children.reserve(pidls.size());
            for (PIDLIST_ABSOLUTE pidl : pidls)
                children.push_back(ILFindLastID(pidl));

            parent->GetUIObjectOf(owner, static_cast<UINT>(children.size()),
                                  children.data(), IID_IContextMenu, nullptr,
                                  reinterpret_cast<void**>(&shell));
        }
    }

    if (shell != nullptr)
    {
        HMENU handle = CreatePopupMenu();
        if (handle != nullptr &&
            SUCCEEDED(shell->QueryContextMenu(handle, 0, kShellCmdFirst, kShellCmdLast, CMF_NORMAL)))
        {
            ReadShellMenu(shell, handle, folderBackground, out, outSkipped);

            if (!out->empty())
            {
                *outMenu   = shell;
                *outHandle = handle;
                shell  = nullptr;
                handle = nullptr;
            }
        }

        if (handle != nullptr)
            DestroyMenu(handle);
        if (shell != nullptr)
            shell->Release();
    }
    if (parent != nullptr)
        parent->Release();

    for (PIDLIST_ABSOLUTE pidl : pidls)
        CoTaskMemFree(pidl);

    return *outMenu != nullptr;
}

// Aggiunge al menu le voci della shell, se ce ne sono. Il blocco resta separato
// dai nostri comandi: sono di un altro, e mescolarli farebbe sembrare nostro
// quello che non lo e'.
void AppendShellItems(AppState& app, ContextMenu& menu)
{
    // La chiave dice **cosa** si e' chiesto, non quando: la stessa selezione nello
    // stesso posto da' gli stessi verbi, e un'estensione non cambia mestiere fra un
    // tasto destro e il successivo.
    const bool folderBackground = app.selection.empty();
    std::wstring key = app.directory;
    key += folderBackground ? L"|sfondo" : L"|selezione";
    for (const std::wstring& name : app.selection)
    {
        key += L"|";
        key += name;
    }

    AppState::ShellMenuCache& cache = app.shellMenuCache;
    // La scadenza c'e' per una ragione precisa: al primo menu della sessione le
    // estensioni moderne di Windows 11 non si sono ancora registrate, e quello che
    // si e' letto e' un elenco **incompleto**. Tenendolo per sempre, "Apri nel
    // Terminale" non comparirebbe mai; tenendolo mezzo minuto, si corregge da solo.
    if (cache.valid && cache.key == key && NowSeconds() - cache.stamp < 30.0)
    {
        DiagLog("--- " + DiagClock() + " verbi della shell **ripresi dalla memoria**: " +
                std::to_string(cache.items.size()) + " voci");
        menu.shell             = cache.shell;
        menu.shellMenu         = cache.handle;
        menu.shellIsBackground = cache.background;
        for (const ContextMenuItem& item : cache.items)
            menu.items.push_back(item);
        menu.items.push_back({"", 0, /*separator=*/true});
        return;
    }

    std::vector<std::wstring> paths = SelectedPaths(app);

    // Nel vuoto il contesto della shell e' **la cartella aperta**: il menu che si
    // apre col tasto destro sul fondo parla della cartella, non di un file. Sono
    // gli stessi verbi su un altro oggetto, ed e' da li' che arrivano "Apri in
    // Terminale", "Condividi" e la finestra delle proprieta' della cartella.
    if (folderBackground)
    {
        if (app.directory.empty())
            return;
        paths.push_back(app.directory);
    }

    const double started = NowSeconds();
    std::vector<ContextMenuItem> items;
    IContextMenu* shell    = nullptr;
    HMENU handle           = nullptr;
    int skipped            = 0;
    // I PIDL servono solo per la selezione: sul fondo della cartella il contesto
    // della shell e' la cartella stessa.
    const std::vector<const BYTE*> pidls = folderBackground ? std::vector<const BYTE*>()
                                                            : SelectedPidls(app);
    BuildShellItems(app.window, paths, pidls, folderBackground, &shell, &handle, &items, &skipped);
    const double elapsed = (NowSeconds() - started) * 1000.0;

    DiagLog("--- " + DiagClock() + (folderBackground ? " sfondo," : " selezione,") + " " +
            std::to_string(paths.size()) + " percorsi, " + std::to_string(items.size()) +
            " voci, " + std::to_string(skipped) + " saltate, " + std::to_string(elapsed) + " ms");

    // Il risultato vecchio si rilascia **dopo** aver costruito il nuovo: se la
    // costruzione fallisce, quello che c'era non e' gia' stato buttato via.
    ReleaseShellMenuCache(app);

    if (shell == nullptr)
    {
        ShellMenuDetach(menu);
        return;
    }

    cache.valid      = true;
    cache.background = folderBackground;
    cache.key        = key;
    cache.items      = items;      // una copia: il menu ne prende la sua
    cache.shell      = shell;
    cache.handle     = handle;
    cache.stamp      = NowSeconds();

    menu.shell             = shell;
    menu.shellMenu         = handle;
    menu.shellIsBackground = folderBackground;
    for (ContextMenuItem& item : items)
        menu.items.push_back(std::move(item));
    // Il separatore va DOPO il blocco: chi lo innesta ci mette il proprio davanti,
    // e in testa a un menu che comincia con la shell sarebbe stato un separatore
    // orfano.
    menu.items.push_back({"", 0, /*separator=*/true});
}

void ContextMenuClose(AppState& app)
{
    ContextMenu& menu = app.contextMenu;
    menu.open = false;
    // I livelli non si svuotano: restano uno solo, vuoto. Un array vuoto
    // significherebbe che il livello 0 non esiste, e ogni funzione di geometria
    // lo indicizza.
    menu.openPath.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.items.clear();
    menu.shellIsBackground = false;
    ShellMenuDetach(menu);   // gli oggetti restano in memoria, per la stessa selezione
}

// Quanti handle di kernel ha in mano il processo. Serve a distinguere un costo da
// una perdita: un'estensione che si carica una volta e' un costo, un handle che
// non torna piu' giu' e' una perdita, e le due cose si vedono uguali contando le
// aperture del menu.
static DWORD OpenHandleCount()
{
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

void BuildContextMenu(AppState& app, float x, float y)
{
    ContextMenu& menu = app.contextMenu;
    const DWORD handlesStart = OpenHandleCount();
    menu.items.clear();
    menu.x = x;
    menu.y = y;
    menu.openPath.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.shellIsBackground = false;

    const size_t count = app.selection.size();
    menu.subtitle = count == 0
                        ? std::string(Tr("niente selezionato", "nothing selected"))
                        : std::to_string(count) +
                              (count == 1 ? Tr(" elemento", " item") : Tr(" elementi", " items"));

    // Il menu si adatta al contesto: nel vuoto restano solo le voci che valgono
    // per la cartella, perche' offrire "Rinomina" senza niente da rinominare e'
    // promettere qualcosa che non si puo' mantenere.
    // In una cartella che non e' un percorso — il cestino — le voci che agiscono su
    // un percorso si spengono invece di sparire: la loro assenza non spiegherebbe
    // niente, e un comando che dice "qui non si puo'" lo dice solo dopo essere
    // stato scelto. Spente, si vede che li' non valgono.
    const bool namespaced = ShellLocation::IsNamespace(app.directory);

    // Li' dentro "Apri" vale per le cartelle (una cartella nel cestino si apre) e
    // non per i file: un file nel cestino non si apre, si ripristina.
    bool cursorIsFile = false;
    if (namespaced && count > 0)
    {
        for (const Entry& entry : app.entries)
        {
            if (entry.wideName == app.cursor)
            {
                cursorIsFile = !entry.isDirectory;
                break;
            }
        }
    }

    if (count > 0)
    {
        menu.items.push_back({Tr("Apri", "Open"), kContextOpen, false, namespaced && cursorIsFile});
        menu.items.push_back({Tr("Apri in Esplora risorse", "Open in Explorer"), CmdOpenExplorer,
                              false, namespaced});
        menu.items.push_back({"", 0, /*separator=*/true});
        menu.items.push_back({Tr("Taglia", "Cut"), CmdCut, false, namespaced});
        menu.items.push_back({Tr("Copia", "Copy"), CmdCopy, false, namespaced});

        // "Invia a" qui e non in fondo: e' dove sta in Esplora risorse, subito
        // dopo il blocco Taglia/Copia, e li' la mano lo cerca.
        DiagLog("  handle: prima di 'Invia a' +" +
                std::to_string(OpenHandleCount() - handlesStart));
        if (!ShellSendTo::Targets().empty())
        {
            ContextMenuItem sendTo;
            sendTo.label      = Tr("Invia a", "Send to");
            sendTo.lazySendTo = true;
            sendTo.disabled   = namespaced;
            menu.items.push_back(std::move(sendTo));
        }
        menu.items.push_back({"", 0, true});
        menu.items.push_back({Tr("Rinomina", "Rename"), CmdRename, false, namespaced});
        menu.items.push_back({Tr("Elimina", "Delete"), CmdDelete, false, namespaced});
        menu.items.push_back({Tr("Elimina definitivamente", "Delete permanently"),
                              CmdDeletePermanent, false, namespaced});
        menu.items.push_back({"", 0, true});
        menu.items.push_back({Tr("Proprietà", "Properties"), kContextProperties, false, namespaced});

        // I verbi della shell stanno dopo i nostri: i nostri sono quelli che si
        // usano davvero, e non devono scendere sotto una lista lunga quanto le
        // estensioni installate su questa macchina.
        menu.items.push_back({"", 0, true});
        AppendShellItems(app, menu);
    }
    else
    {
        // Col tasto destro sul fondo, invece, la shell apre il menu: quello e' il
        // contesto di cio' su cui si e' cliccato.
        AppendShellItems(app, menu);
    }

    menu.items.push_back({count > 0 ? Tr("Incolla qui", "Paste here") : Tr("Incolla", "Paste"),
                          CmdPaste, false, namespaced});
    menu.items.push_back({Tr("Nuova cartella", "New folder"), CmdNewFolder, false, namespaced});

    // "Nuovo file": i tipi che Windows sa creare, dal registro. Se il registro non
    // ne ha nessuno la voce non si mette, perche' un sottomenu vuoto non si spiega.
    DiagLog("  handle: dopo 'Invia a' e i verbi +" +
            std::to_string(OpenHandleCount() - handlesStart));
    if (!ShellNew::Templates().empty())
    {
        ContextMenuItem newFile;
        newFile.label       = Tr("Nuovo file", "New file");
        newFile.lazyNewFile = true;
        newFile.disabled    = namespaced;
        menu.items.push_back(std::move(newFile));
    }

    menu.items.push_back({"", 0, true});
    menu.items.push_back({Tr("Aggiorna", "Refresh"), CmdRefresh});
    menu.items.push_back({Tr("Apri un terminale qui", "Open a console here"), CmdOpenConsole, false,
                          namespaced});

    DiagLog("  handle: dopo 'Nuovo file' +" +
            std::to_string(OpenHandleCount() - handlesStart));
    menu.open = true;
}

void ContextMenuMove(AppState& app, const Layout& layout, int delta)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int count = LevelCount(menu, depth);
    if (count == 0 || delta == 0)
        return;

    // Niente e' selezionato all'apertura, e la prima freccia deve scegliere la
    // PRIMA voce. Partendo da 0 l'incremento del ciclo salterebbe proprio quella
    // e la selezione comincerebbe dalla seconda: l'errore era invisibile con la
    // prima voce separata da un separatore e non con le altre.
    //
    // Le frecce saltano i separatori e le voci spente: fermarcisi sopra non vuol
    // dire niente e l'Invio non avrebbe nulla da eseguire.
    int index = level.selected < 0 ? (delta > 0 ? -1 : count) : level.selected;
    for (int step = 0; step < count; ++step)
    {
        index = (index + delta + count * 2) % count;
        const ContextMenuItem* item = LevelItem(menu, depth, index);
        if (item != nullptr && !item->separator && !item->disabled)
            break;
    }
    level.selected = index;

    // Se la voce scelta sta fuori dal pannello, il menu scivola di quel tanto.
    ContextMenuClampWindow(app, layout, depth);
}

// Porta la scelta alla prima o all'ultima voce eseguibile.
void ContextMenuJump(AppState& app, const Layout& layout, bool toFirst)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int count = LevelCount(menu, depth);
    if (count == 0)
        return;

    const int step = toFirst ? 1 : -1;
    int index = toFirst ? -1 : count;
    for (int i = 0; i < count; ++i)
    {
        index = (index + step + count * 2) % count;
        const ContextMenuItem* item = LevelItem(menu, depth, index);
        if (item != nullptr && !item->separator && !item->disabled)
            break;
    }
    level.selected = index;
    ContextMenuClampWindow(app, layout, depth);
}

// Apre la finestra delle proprieta' di Windows sulla selezione: la stessa che
// apre Esplora risorse, quindi schede, permessi, versioni e anteprime sono quelli
// del sistema e non una nostra imitazione. Restituisce quante ne ha aperte.
int ShowProperties(HWND owner, const std::vector<std::wstring>& paths)
{
    int launched = 0;
    for (const std::wstring& path : paths)
    {
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask  = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
        info.hwnd   = owner;
        info.lpVerb = L"properties";
        info.lpFile = path.c_str();
        info.nShow  = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info))
            ++launched;
    }
    return launched;
}

// --- il menu "Nuovo file", che facciamo noi ---------------------------------
//
// La shell il proprio "Nuovo" lo popola in un sottomenu, e i comandi di quel
// sottomenu non si possono eseguire da qui (vedi `IsShellOwnSubmenuVerb`), quindi
// il menu "Nuovo file" se lo costruisce Bolt dalle stesse fonti che usa la shell:
// le chiavi `ShellNew` del registro. Vedi `docs/04-roadmap.md`, sezione M7.

// Riempie il sottomenu coi modelli del registro: un CommandId della banda per
// ciascuno, che e' la sua posizione nell'elenco.
void FillNewFileMenu(ContextMenuItem& item)
{
    const double started = NowSeconds();
    const std::vector<ShellNew::Template>& templates = ShellNew::Templates();
    const double elapsed = (NowSeconds() - started) * 1000.0;

    for (size_t i = 0; i < templates.size(); ++i)
    {
        ContextMenuItem entry;
        entry.label   = ShellLabelToAscii(templates[i].label);
        entry.command = kNewItemCmdFirst + static_cast<int>(i);
        item.children.push_back(std::move(entry));
        DiagLog("    nuovo file: '" + item.children.back().label + "'");
    }

    DiagLog("  modelli di 'Nuovo file': " + std::to_string(templates.size()) + " voci, " +
            std::to_string(elapsed) + " ms");
}

// --- il menu "Invia a", che facciamo noi -------------------------------------
//
// Stessa storia del "Nuovo": la shell il proprio "Invia a" lo popola in un
// sottomenu, e quei comandi non si possono eseguire da fuori. La differenza e' che
// qui la fonte non e' nascosta: "Invia a" e' **una cartella vera**
// (`%APPDATA%\Microsoft\Windows\SendTo`), quindi si legge e si esegue a mano.
// Vedi `src/shell_sendto.h`.

// Riempie il sottomenu con le destinazioni: un CommandId della banda per ciascuna,
// che e' la sua posizione nell'elenco letto adesso.
void FillSendToMenu(ContextMenuItem& item)
{
    const double started = NowSeconds();
    const std::vector<ShellSendTo::Target> targets = ShellSendTo::Targets();
    const double elapsed = (NowSeconds() - started) * 1000.0;

    for (size_t i = 0; i < targets.size(); ++i)
    {
        ContextMenuItem entry;
        entry.label   = ShellLabelToAscii(targets[i].label);
        entry.command = kSendToCmdFirst + static_cast<int>(i);
        item.children.push_back(std::move(entry));
    }

    // Deliberatamente **senza le etichette**: l'elenco delle destinazioni dice
    // quali programmi ha installato chi usa questo computer, e nel registro
    // diagnostico non ci finisce. Vedi `AGENTS.md` §0.
    DiagLog("  destinazioni di 'Invia a': " + std::to_string(targets.size()) + " voci, " +
            std::to_string(elapsed) + " ms");
}

// Lascia cadere la selezione su una voce della cartella SendTo.
//
// Non si avvia con `ShellExecuteEx`, e la ragione e' nel registro: la voce
// "cartella compressa" della cartella SendTo **non e' un programma**, e' un file
// `.ZFSendToTarget` che contiene un CLSID, e il suo handler e' registrato come
// `DropHandler` — il registro lo dice con le parole "Drag Files onto this icon to
// compress them". Avviarla non le da' niente da comprimere: `ShellExecuteEx`
// restituisce TRUE e non succede niente, che e' la peggiore delle risposte.
//
// Quindi si fa quello che farebbe il mouse: si prende il bersaglio di caduta della
// voce e gli si lascia cadere sopra la selezione. Vale per tutte le voci della
// cartella, compresi i collegamenti ai programmi, che si aprono con i file
// ricevuti — e' quello che fa la shell quando ci si trascina sopra qualcosa.
bool DropOnSendToTarget(HWND owner, const std::wstring& path, IDataObject* data)
{
    if (data == nullptr)
        return false;

    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr)) || item == nullptr)
        return false;

    PIDLIST_ABSOLUTE parent = ILClone(item);
    bool dropped = false;

    IShellFolder* folder = nullptr;
    if (parent != nullptr)
    {
        ILRemoveLastID(parent);
        if (SUCCEEDED(SHBindToObject(nullptr, parent, nullptr, IID_IShellFolder,
                                    reinterpret_cast<void**>(&folder))) &&
            folder != nullptr)
        {
            PCUITEMID_CHILD child = ILFindLastID(item);
            IDropTarget* target = nullptr;
            if (SUCCEEDED(folder->GetUIObjectOf(owner, 1, &child, IID_IDropTarget, nullptr,
                                                reinterpret_cast<void**>(&target))) &&
                target != nullptr)
            {
                // `DROPEFFECT_COPY`: "invia a" manda una copia, non sposta — e'
                // quello che fa la shell, ed e' anche quello che serve alla
                // cartella compressa, che legge i file senza portarli via.
                POINTL point{};
                DWORD  effect = DROPEFFECT_COPY;
                if (SUCCEEDED(target->DragEnter(data, MK_LBUTTON, point, &effect)))
                {
                    effect = DROPEFFECT_COPY;
                    dropped = SUCCEEDED(target->Drop(data, MK_LBUTTON, point, &effect));
                    DiagLog("  caduta su voce: effetto restituito " + std::to_string(effect));
                }
                target->Release();
            }
            folder->Release();
        }
        CoTaskMemFree(parent);
    }
    CoTaskMemFree(item);
    return dropped;
}

// Manda la selezione a una destinazione di "Invia a".
void SendToTargets(AppState& app, size_t index, const std::vector<std::wstring>& paths)
{
    const std::vector<ShellSendTo::Target> targets = ShellSendTo::Targets();
    if (index >= targets.size())
        return;
    if (paths.empty())
        return;   // mandare "niente" da qualche parte non e' un'azione

    IDataObject* data = DragDrop::DataObjectFor(paths);
    const bool dropped = DropOnSendToTarget(app.window, targets[index].path, data);
    if (data != nullptr)
        data->Release();

    if (!dropped)
    {
        // Nel registro va l'indice, **non l'etichetta**: le destinazioni dicono
        // quali programmi ha installato chi usa il computer. Vedi `AGENTS.md` §0.
        DiagLog("  invio a voce " + std::to_string(index) + ": non riuscito");
        ReportStatus(app, L"\"" + targets[index].label + L"\" non ha accettato i file.");
        return;
    }

    DiagLog("  invio a voce " + std::to_string(index) + ": riuscito");

    // "Avviato", non "fatto": la destinazione puo' copiare, comprimere o aprire
    // una finestra sua, e il suo esito non e' nostro da dichiarare.
    ReportStatus(app, L"Avviato: " + targets[index].label);
}

// Chiede alla shell il contenuto di un sottomenu e lo copia nel nostro albero.
//
// Un sottomenu della shell **non ha contenuto** finche' non arriva
// WM_INITMENUPOPUP: leggerlo prima vuol dire leggere un menu vuoto e concludere
// che la voce non ha niente dentro. E il messaggio va mandato alla voce giusta,
// perche' la shell decide COSA popolare in base alla posizione della voce nel
// menu padre — "Invia a" di un file non e' quello di una cartella.
void FillShellSubmenu(AppState& app, ContextMenuItem& item)
{
    if (item.shellSubmenu == nullptr || !item.children.empty())
        return;
    if (app.contextMenu.shell == nullptr)
        return;

    IContextMenu2* menu2 = nullptr;
    if (SUCCEEDED(app.contextMenu.shell->QueryInterface(IID_IContextMenu2,
                                                       reinterpret_cast<void**>(&menu2))))
    {
        menu2->HandleMenuMsg(WM_INITMENUPOPUP,
                             reinterpret_cast<WPARAM>(item.shellSubmenu),
                             static_cast<LPARAM>(item.shellPosition));
        menu2->Release();
    }

    int skipped = 0;
    ReadShellMenu(app.contextMenu.shell, item.shellSubmenu,
                  app.contextMenu.shellIsBackground, &item.children, &skipped);
    DiagLog("    sottomenu di '" + item.label + "': " + std::to_string(item.children.size()) +
            " voci, " + std::to_string(skipped) + " saltate");
}

// Tiene i livelli da 0 a |depth| compreso e butta via quelli sotto.
void CloseSubmenusBelow(ContextMenu& menu, int depth)
{
    const int wanted = depth + 1;
    if (static_cast<int>(menu.levels.size()) <= wanted)
        return;
    menu.openPath.resize(static_cast<size_t>(depth));
    menu.levels.resize(static_cast<size_t>(wanted));
}

// Apre il sottomenu della voce |index| del livello |depth|. Restituisce falso se
// quella voce un sottomenu non ce l'ha, o se e' rimasto vuoto.
bool OpenSubmenu(AppState& app, const Layout& layout, int depth, int index)
{
    ContextMenu& menu = app.contextMenu;
    ContextMenuItem* item = LevelItemMutable(menu, depth, index);
    if (item == nullptr || item->disabled || !HasSubmenu(*item))
        return false;

    // Gia' aperto esattamente cosi': non si rifa' niente. I movimenti del mouse
    // arrivano a decine al secondo, e riaprire il sottomenu a ogni movimento
    // azzererebbe la scelta che ci si e' appena spostati a fare.
    if (static_cast<int>(menu.openPath.size()) > depth &&
        menu.openPath[static_cast<size_t>(depth)] == index)
        return true;

    if (item->lazyNewFile && item->children.empty())
        FillNewFileMenu(*item);
    if (item->lazySendTo && item->children.empty())
        FillSendToMenu(*item);

    FillShellSubmenu(app, *item);
    if (item->children.empty())
    {
        // Un sottomenu che resta vuoto non si apre: si vedrebbe un pannello vuoto
        // e non si capirebbe se e' colpa nostra o della shell.
        DiagLog("  sottomenu vuoto: '" + item->label + "'");
        return false;
    }

    menu.openPath.resize(static_cast<size_t>(depth) + 1);
    menu.openPath[static_cast<size_t>(depth)] = index;
    menu.levels.resize(static_cast<size_t>(depth) + 2);

    // La scelta cade sulla prima voce eseguibile: subito dopo si sta li' con le
    // frecce, e un sottomenu aperto senza niente scelto non dice dove si e'.
    ContextMenuLevel& level = menu.levels.back();
    level.selected  = -1;
    level.firstItem = 0;

    const int newDepth = static_cast<int>(menu.openPath.size());
    const int count = LevelCount(menu, newDepth);
    for (int i = 0; i < count; ++i)
    {
        const ContextMenuItem* child = LevelItem(menu, newDepth, i);
        if (child != nullptr && !child->separator && !child->disabled)
        {
            level.selected = i;
            break;
        }
    }
    ContextMenuClampWindow(app, layout, newDepth);
    return true;
}

// Esc e freccia sinistra: si risale di un livello, e solo dal primo livello si
// chiude il menu. E' l'unico modo di uscire da un sottomenu senza eseguire
// niente, ed e' quello che fanno tutti i menu.
void ContextMenuRaiseLevel(AppState& app)
{
    const int depth = ActiveDepth(app.contextMenu);
    if (depth == 0)
        ContextMenuClose(app);
    else
        CloseSubmenusBelow(app.contextMenu, depth - 1);
}

void ContextMenuActivate(AppState& app)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    const ContextMenuItem* chosen = LevelItem(menu, depth,
                                              menu.levels[static_cast<size_t>(depth)].selected);
    if (chosen == nullptr || chosen->separator || chosen->disabled)
        return;

    // Una voce con un sottomenu non esegue niente: lo apre, e ci si lavora
    // dentro. E' l'Invio che scende, come nei menu di Windows.
    if (HasSubmenu(*chosen))
    {
        OpenSubmenu(app, ComputeLayout(app), depth,
                    menu.levels[static_cast<size_t>(depth)].selected);
        return;
    }

    // Una voce di "Nuovo file": la posizione nella banda e' l'indice del modello
    // letto dal registro. Il menu si chiude come per ogni altra voce: senza questo
    // resterebbe aperto sopra la rinomina che parte subito dopo.
    if (chosen->command >= kNewItemCmdFirst && chosen->command < kNewItemCmdLast)
    {
        const size_t model = static_cast<size_t>(chosen->command - kNewItemCmdFirst);
        ContextMenuClose(app);
        CreateNewItem(app, model);
        return;
    }

    // Una destinazione di "Invia a": stessa forma — la posizione nella banda e'
    // l'indice nell'elenco letto dalla cartella SendTo.
    if (chosen->command >= kSendToCmdFirst && chosen->command < kSendToCmdLast)
    {
        const size_t target = static_cast<size_t>(chosen->command - kSendToCmdFirst);
        const std::vector<std::wstring> paths = SelectedPaths(app);
        ContextMenuClose(app);
        SendToTargets(app, target, paths);
        return;
    }

    const ContextMenuItem item = *chosen;
    const std::vector<std::wstring> paths = SelectedPaths(app);

    // Un id nella banda riservata appartiene alla shell, e l'IContextMenu serve
    // ancora al momento dell'esecuzione: lo si prende prima di chiudere, perche'
    // chiudere lo rilascia.
    IContextMenu* shell  = app.contextMenu.shell;
    const int     offset = item.command - kShellCmdFirst;
    if (shell != nullptr)
        shell->AddRef();
    ContextMenuClose(app);

    if (item.command >= kShellCmdFirst)
    {
        if (shell == nullptr)
        {
            ReportStatus(app, L"Comando della shell non piu' disponibile.");
            return;
        }

        CMINVOKECOMMANDINFOEX info{};
        info.cbSize  = sizeof(info);
        info.fMask   = CMIC_MASK_UNICODE | CMIC_MASK_FLAG_NO_UI;
        info.hwnd    = app.window;
        info.lpVerb  = MAKEINTRESOURCEA(static_cast<WORD>(offset));
        info.lpVerbW = MAKEINTRESOURCEW(static_cast<WORD>(offset));
        info.nShow   = SW_SHOWNORMAL;

        const HRESULT hr = shell->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
        shell->Release();

        // Un'azione che fallisce in silenzio e' peggio di una che non parte.
        if (FAILED(hr))
        {
            ReportStatus(app, L"Il comando della shell non e' partito.");
            return;
        }

        // Un verbo della shell puo' aver cambiato la cartella: "Ripristina" toglie la
        // voce dal cestino, "Estrai qui" aggiunge dei file. La vista si rifa', e la
        // selezione si tiene per nome: quello che non c'e' piu' sparisce da solo.
        ReloadKeeping(app, app.selection, app.cursor, app.scroll);
        return;
    }

    if (item.command == kContextOpen)
    {
        ActivateSelection(app);
        return;
    }
    if (item.command == kContextProperties)
    {
        // Un'azione che non fa niente in silenzio e' peggio di una che non parte:
        // se non si apre niente, l'utente deve sapere perche'.
        if (paths.empty())
            ReportStatus(app, L"Niente su cui aprire le proprieta'.");
        else if (ShowProperties(app.window, paths) == 0)
            ReportStatus(app, L"Proprietà non disponibili per la selezione.");
        return;
    }

    // Tutto il resto e' un comando che esiste gia': il menu contestuale non
    // aggiunge capacita', rende raggiungibile dove serve quella che c'e'.
    RunCommand(app, item.command);
}

// Disegna un livello del menu. Il livello 0 e' il menu, gli altri i sottomenu:
// stesso pannello, senza sottotitolo, e col segno che dice quali voci ne hanno
// uno dentro.
void ContextMenuDrawLevel(AppState& app, const Layout& layout, int depth)
{
    ContextMenu& menu = app.contextMenu;
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = ContextMenuPanel(app, layout, depth);
    const ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];

    r.Draw(UIDraw::Shadow(Rect{panel.x + 2.0f * scale, panel.y + 5.0f * scale, panel.w, panel.h},
                          Color{0.0f, 0.0f, 0.0f, 0.5f}, 20.0f * scale, 8.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 8.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 8.0f * scale));

    // Solo le voci dentro la finestra del menu: quelle fuori non si disegnano, e
    // quello che non si disegna non sborda fuori dal pannello.
    for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
    {
        const ContextMenuItem* item = LevelItem(menu, depth, i);
        if (item == nullptr)
            continue;
        const Rect row = ContextMenuRowRect(app, layout, depth, i);

        if (item->separator)
        {
            r.FillRect(Rect{row.x + 6.0f * scale, row.y + row.h * 0.5f,
                            row.w - 12.0f * scale, 1.0f},
                       t.separator);
            continue;
        }

        if (i == level.selected && !item->disabled)
            r.FillRect(row, t.rowHover, 4.0f * scale);

        app.font.Draw(r, row.x + 10.0f * scale, TextY(layout, row, app.font), item->label,
                      item->disabled ? t.textDim : t.text, scale);

        // Il segno che la voce ne contiene altre. L'atlante dei glifi e' ASCII,
        // quindi e' un '>' e non una punta: si legge uguale, e non chiede un
        // carattere che l'atlante non ha.
        if (HasSubmenu(*item))
            app.font.Draw(r, row.right() - 18.0f * scale, TextY(layout, row, app.font), ">",
                          t.textDim, scale);
    }

    // La barra dice che sopra o sotto c'e' dell'altro: un menu che scorre senza
    // dirlo sembra un menu a cui mancano delle voci.
    const int total   = LevelCount(menu, depth);
    const int visible = ContextMenuLastVisible(app, layout, depth) - level.firstItem;
    if (visible < total)
    {
        const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
        const float trackY = panel.y + 6.0f * scale;
        const float trackH = panel.h - 12.0f * scale - footer;
        const float thumbH = trackH * static_cast<float>(visible) / static_cast<float>(total);
        const float travel = trackH - thumbH;
        const float offset = static_cast<float>(level.firstItem) /
                             static_cast<float>((std::max)(1, total - visible));
        r.FillRect(Rect{panel.right() - 7.0f * scale, trackY, 3.0f * scale, trackH}, t.separator);
        r.FillRect(Rect{panel.right() - 7.0f * scale, trackY + travel * offset,
                        3.0f * scale, thumbH},
                   t.textDim);
    }

    // Il sottotitolo e' del primo livello soltanto: un sottomenu non conta niente.
    if (depth == 0)
        app.smallFont.Draw(r, panel.x + 14.0f * scale, panel.bottom() - 17.0f * scale,
                           menu.subtitle, t.textDim, scale);
}

void ContextMenuDraw(AppState& app, const Layout& layout)
{
    if (!app.contextMenu.open)
        return;

    // Dal primo livello in giu': ogni pannello disegna sopra il precedente, che e'
    // l'ordine in cui stanno sullo schermo.
    for (int depth = 0; depth <= ActiveDepth(app.contextMenu); ++depth)
        ContextMenuDrawLevel(app, layout, depth);
}

// Vero se il clic e' stato consumato dal menu.
bool ContextMenuClick(AppState& app, const Layout& layout, float x, float y)
{
    if (!app.contextMenu.open)
        return false;

    ContextMenu& menu = app.contextMenu;

    // Dal livello piu' profondo al primo: il pannello aperto per ultimo sta sopra
    // gli altri, quindi e' lui che riceve il clic.
    for (int depth = ActiveDepth(menu); depth >= 0; --depth)
    {
        if (!ContextMenuPanel(app, layout, depth).Contains(x, y))
            continue;

        ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
        for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
        {
            if (!ContextMenuRowRect(app, layout, depth, i).Contains(x, y))
                continue;
            level.selected = i;
            ContextMenuActivate(app);
            return true;
        }
        return true;   // dentro il pannello, ma non su una voce
    }

    ContextMenuClose(app);
    return true;   // un clic fuori chiude, e non deve arrivare alla lista
}

// Freccia destra: scende nel sottomenu della voce scelta. Se non ce l'ha non fa
// niente — una voce semplice non e' un errore.
void ContextMenuDescend(AppState& app, const Layout& layout)
{
    const ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    const int index = menu.levels[static_cast<size_t>(depth)].selected;
    if (index < 0)
        return;
    OpenSubmenu(app, layout, depth, index);
}

// Con il menu aperto il puntatore sceglie la voce, non la riga sotto: la lista e'
// coperta, e sovrappassarla sarebbe un secondo stato che l'utente non vede.
//
// Il puntatore sceglie anche il LIVELLO: entrando in un pannello piu' profondo si
// agisce su quello, e su una voce che contiene un sottomenu il sottomenu si apre
// da solo. Col mouse e' cosi' che si usano i menu, ed e' anche l'unico modo di
// accorgersi che quella voce ne aveva uno.
bool ContextMenuHover(AppState& app, const Layout& layout, float x, float y)
{
    ContextMenu& menu = app.contextMenu;
    if (!menu.open)
        return false;

    for (int depth = ActiveDepth(menu); depth >= 0; --depth)
    {
        if (!ContextMenuPanel(app, layout, depth).Contains(x, y))
            continue;

        ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
        for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
        {
            const ContextMenuItem* item = LevelItem(menu, depth, i);
            if (item == nullptr || item->separator || item->disabled ||
                !ContextMenuRowRect(app, layout, depth, i).Contains(x, y))
                continue;

            level.selected = i;
            if (HasSubmenu(*item))
                OpenSubmenu(app, layout, depth, i);
            else
                CloseSubmenusBelow(menu, depth);   // questa voce non ne contiene altri
            return true;
        }

        // Dentro il pannello ma non su una voce: la scelta di QUESTO livello cade,
        // i livelli sotto restano dove sono.
        level.selected = -1;
        return true;
    }

    // Fuori da ogni pannello non si tocca niente: la scelta della tastiera non
    // deve cadere perche' il puntatore e' andato altrove.
    return true;
}

// Il disegno del controllo dentro una riga di opzione. Il tipo decide la forma,
// e una opzione spenta si vede spenta invece di sparire: e' la lezione del
// riferimento, dove `System Light / Dark Scheme` sta li' ma non si tocca.
void DrawOptionControl(AppState& app, const Layout& layout, const GroupMenuItem& item,
                       const Rect& control)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Color ink  = item.enabled ? t.text : t.textDim.WithAlpha(0.55f);
    const Color line = item.enabled ? t.panelBorder : t.separator;

    switch (item.control)
    {
        case OptionControl::Toggle:
        {
            const Color track = item.enabled ? (item.toggleOn ? t.accent : t.panelBorder) : t.separator;
            r.Draw(UIDraw::Fill(control, track, control.h * 0.5f));
            const float knob = control.h - 4.0f * scale;
            const float x = item.toggleOn ? control.right() - knob - 2.0f * scale
                                          : control.x + 2.0f * scale;
            r.Draw(UIDraw::Fill(Rect{x, control.y + 2.0f * scale, knob, knob},
                                item.enabled ? Color{0.98f, 0.99f, 1.0f, 1.0f}
                                             : t.textDim.WithAlpha(0.55f), knob * 0.5f));
            break;
        }
        case OptionControl::Stepper:
        {
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, line, 1.0f, 4.0f * scale));
            const float box = control.w / 3.0f;
            const float textY = TextY(layout, control, app.font);
            app.font.Draw(r, control.x + box * 0.42f, textY, "-", ink, scale);
            app.font.Draw(r, control.right() - box * 0.58f, textY, "+", ink, scale);
            const float w = app.font.Measure(item.valueText, scale);
            app.font.Draw(r, control.x + (control.w - w) * 0.5f, textY, item.valueText, ink, scale);
            break;
        }
        case OptionControl::Slider:
        {
            const float knob   = control.h * 0.5f;
            const float trackH = 4.0f * scale;
            const Rect  track{control.x + knob, control.y + (control.h - trackH) * 0.5f,
                              (std::max)(1.0f, control.w - knob * 2.0f), trackH};
            r.Draw(UIDraw::Fill(track, item.enabled ? t.panelBorder : t.separator, trackH * 0.5f));
            float fraction = item.fraction;
            fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
            const float filled = track.w * fraction;
            if (filled > 0.0f)
                r.Draw(UIDraw::Fill(Rect{track.x, track.y, filled, track.h},
                                    item.enabled ? t.accent : t.separator, trackH * 0.5f));
            r.Draw(UIDraw::Fill(Rect{track.x + filled - knob, control.y, knob * 2.0f, knob * 2.0f},
                                item.enabled ? Color{0.95f, 0.96f, 1.0f, 1.0f}
                                             : t.textDim.WithAlpha(0.55f), knob));
            break;
        }
        case OptionControl::Text:
        {
            const bool editing = app.optionEditActive && app.optionEditId == item.id;
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, editing ? t.accent : line, 1.0f, 4.0f * scale));
            const std::string shown = editing ? app.optionEditText : item.valueText;
            const float textY = TextY(layout, control, app.font);
            app.font.Draw(r, control.x + 8.0f * scale, textY, shown, ink, scale);
            if (editing)
            {
                const float caretX = control.x + 8.0f * scale + app.font.Measure(shown, scale) + 1.0f * scale;
                r.FillRect(Rect{caretX, control.y + 4.0f * scale, 1.0f, control.h - 8.0f * scale}, t.text);
            }
            break;
        }
        case OptionControl::Choice:
        {
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, line, 1.0f, 4.0f * scale));
            const float textY = TextY(layout, control, app.smallFont);
            app.smallFont.Draw(r, control.x + 10.0f * scale, textY, item.valueText, ink, scale);
            app.smallFont.Draw(r, control.right() - 16.0f * scale,
                               TextY(layout, control, app.smallFont), ">", ink, scale);
            break;
        }
        default:
            break;
    }
}

void GroupMenuDraw(AppState& app, const Layout& layout, GroupMenu& menu)
{
    if (!menu.open)
        return;

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = MenuPanel(app, layout, menu);

    r.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                    static_cast<float>(app.viewportHeight)}, Color{0.0f, 0.0f, 0.0f, 0.45f});

    r.Draw(UIDraw::Shadow(Rect{panel.x + 3.0f * scale, panel.y + 6.0f * scale, panel.w, panel.h},
                          Color{0.0f, 0.0f, 0.0f, 0.45f}, 22.0f * scale, 10.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 10.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 10.0f * scale));

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.y + 12.0f * scale,
                  menu.title, t.textDim, scale);

    const std::string query = menu.filter.empty() ? menu.placeholder : ToUtf8(menu.filter);
    app.font.Draw(r, panel.x + 16.0f * scale, panel.y + 32.0f * scale, query,
                  menu.filter.empty() ? t.textDim : t.text, scale);
    if (!menu.filter.empty())
    {
        const float caretX = panel.x + 16.0f * scale + app.font.Measure(query, scale) + 1.0f * scale;
        r.FillRect(Rect{caretX, panel.y + 32.0f * scale, 1.0f, app.font.LineHeight() * scale}, t.text);
    }

    const float listTop    = panel.y + 58.0f * scale;
    const float listBottom = panel.bottom() - 26.0f * scale;
    const float rowHeight  = MenuRowHeight(layout, menu);
    r.FillRect(Rect{panel.x + 1.0f, listTop - 4.0f * scale, panel.w - 2.0f, 1.0f}, t.separator);

    const int firstVisible =
        GroupMenuFirstVisible(menu, listTop, listBottom, rowHeight);

    for (int i = firstVisible; i < static_cast<int>(menu.rows.size()); ++i)
    {
        const float y = listTop + (i - firstVisible) * rowHeight;
        if (y + rowHeight > listBottom)
            break;

        const GroupMenuRow& row = menu.rows[static_cast<size_t>(i)];
        const Rect rowRect{panel.x + 6.0f * scale, y, panel.w - 12.0f * scale, rowHeight};

        if (row.header)
        {
            const std::string& name = menu.groups[static_cast<size_t>(row.group)];
            const bool collapsed = menu.collapsed.count(name) != 0;
            r.FillRect(rowRect, t.headerBg, 3.0f * scale);
            app.smallFont.Draw(r, rowRect.x + 8.0f * scale, TextY(layout, rowRect, app.smallFont),
                          (collapsed ? "> " : "v ") + name, t.text, scale);

            ClickTarget target;
            target.rect  = rowRect;
            target.kind  = ClickTarget::Kind::MenuHeader;
            target.label = name;
            app.clickTargets.push_back(target);
            continue;
        }

        const GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];
        if (i == menu.selected)
            r.FillRect(rowRect, t.rowSelected, 3.0f * scale);

        // La riga del pannello delle opzioni: nome, descrizione e controllo. Non
        // lascia un bersaglio da clic perche' la geometria la rifa' `OptionsClick`:
        // due calcoli della stessa riga sono due occasioni di non essere d'accordo.
        if (menu.richRows)
        {
            const Color nameInk = item.enabled ? t.text : t.textDim.WithAlpha(0.6f);
            const Color descInk = item.enabled ? t.textDim : t.textDim.WithAlpha(0.45f);
            app.font.Draw(r, rowRect.x + 14.0f * scale, rowRect.y + 7.0f * scale,
                          item.label, nameInk, scale);
            const Rect control = OptionControlRect(layout, item, rowRect);
            const float descMax = control.x - (rowRect.x + 14.0f * scale) - 16.0f * scale;
            if (descMax > 24.0f)
                app.smallFont.Draw(r, rowRect.x + 14.0f * scale, rowRect.y + 27.0f * scale,
                                   FitText(app.smallFont, item.description, scale, descMax),
                                   descInk, scale);
            DrawOptionControl(app, layout, item, control);
            continue;
        }

        // L'etichetta e il dettaglio si dividono la riga. Il dettaglio, quando c'e',
        // prende al massimo poco piu' di meta' riga e si taglia **davanti** — di un
        // percorso la parte che dice dove si va e' la coda; l'etichetta prende quello
        // che resta e si taglia in fondo. Con un carattere largo scelto dalle opzioni
        // i due si sovrapponevano, e due testi uno sopra l'altro non si leggono.
        std::string label = item.label;
        if (menu.detailed)
        {
            // Il dettaglio prende quello che avanza dopo l'etichetta, e si taglia
            // **davanti** — di un percorso la parte che dice dove si va e' la coda.
            // Se non avanza niente, si tiene una scheggia e si accorcia l'etichetta:
            // meglio due testi troncati che due testi sovrapposti.
            const std::string& raw = item.detail.empty() ? item.group : item.detail;
            const float labelNeed = app.font.Measure(label, scale);
            const float detailRoom = (std::max)(60.0f * scale,
                                                rowRect.w - labelNeed - 34.0f * scale);
            const std::string detail = FitTextTail(app.smallFont, raw, scale, detailRoom);
            const float width = app.smallFont.Measure(detail, scale);
            app.smallFont.Draw(r, rowRect.right() - width - 12.0f * scale,
                               TextY(layout, rowRect, app.smallFont), detail, t.textDim, scale);
            const float room = rowRect.right() - width - 20.0f * scale - (rowRect.x + 14.0f * scale);
            label = FitText(app.font, label, scale, (std::max)(0.0f, room));
        }
        else
        {
            label = FitText(app.font, label, scale, rowRect.w - 28.0f * scale);
        }

        app.font.Draw(r, rowRect.x + 14.0f * scale, TextY(layout, rowRect, app.font),
                      label, t.text, scale);

        ClickTarget target;
        target.rect  = rowRect;
        target.kind  = ClickTarget::Kind::MenuRow;
        target.index = row.item;
        app.clickTargets.push_back(target);
    }

    // La barra di scorrimento, quando le righe non ci stanno tutte: con venti
    // opzioni il cursore va giu' ma il pollice e' l'unica cosa che dice quanto
    // lungo sia l'elenco.
    if (menu.richRows)
    {
        const float total = static_cast<float>(menu.rows.size()) * rowHeight;
        const float room  = listBottom - listTop;
        if (total > room)
        {
            const float barW = 4.0f * scale;
            const Rect  track{panel.right() - 10.0f * scale, listTop, barW, room};
            r.Draw(UIDraw::Fill(track, t.separator, barW * 0.5f));
            const float thumbH = (std::max)(24.0f * scale, room * room / total);
            const float maxOffset = (std::max)(1.0f, total - room);
            const float offset = (firstVisible * rowHeight) / maxOffset;
            r.Draw(UIDraw::Fill(Rect{track.x, track.y + (room - thumbH) * offset, barW, thumbH},
                                t.scrollThumb, barW * 0.5f));
        }
    }

    const std::string footer = std::to_string(menu.matchCount) + " of " +
                               std::to_string(menu.items.size()) + "   " + menu.hint;
    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.bottom() - 20.0f * scale,
                  footer, t.textDim, scale);
}

void DrawFrame(AppState& app, const Layout& layout)
{
    app.clickTargets.clear();

    float columnX[kColumnCount] = {};

    app.renderer.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                               static_cast<float>(app.viewportHeight)}, app.theme.windowBg);

    DrawTitleBar(app, layout);
    DrawToolbar(app, layout, LocationLabel(app.directory));
    DrawSidebar(app, layout);
    DrawHeader(app, layout, columnX);

    // La lista disegna **solo dentro il suo rettangolo**, e il ritaglio lo fa il
    // scissor. Il controllo che salta le righe fuori dal riquadro non basta: salta
    // quelle che stanno fuori tutte intere, non taglia quelle a meta' — e una cella
    // di griglia e' alta quanto una miniatura, quindi il pezzo che esce dal bordo
    // finisce **sopra la barra del titolo**. Si vedeva: quattro icone disegnate
    // sopra le schede, con la riga di nomi piu' in basso.
    //
    // I quadrilateri si accumulano in un batch solo, quindi il batch va chiuso e
    // riaperto attorno al ritaglio: tre disegni per fotogramma invece di uno. E' il
    // prezzo del ritaglio, e si paga.
    app.renderer.End();
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(layout.list.x),
              static_cast<GLint>(static_cast<float>(app.viewportHeight) - layout.list.bottom()),
              static_cast<GLsizei>(layout.list.w), static_cast<GLsizei>(layout.list.h));
    app.renderer.Begin(app.viewportWidth, app.viewportHeight);

    DrawList(app, layout, columnX);

    app.renderer.End();
    glDisable(GL_SCISSOR_TEST);
    app.renderer.Begin(app.viewportWidth, app.viewportHeight);

    // The rubber band is drawn over the rows it is about to select, and only over
    // the list: it is clamped to that region so dragging past the edge does not
    // look like it is selecting the sidebar.
    if (app.rectSelecting)
    {
        const float x0 = (std::min)(app.rectX0, app.rectX1);
        const float y0 = (std::min)(app.rectY0, app.rectY1);
        const Rect band{x0, y0, std::fabs(app.rectX1 - app.rectX0), std::fabs(app.rectY1 - app.rectY0)};
        app.renderer.FillRect(band, app.theme.accent.WithAlpha(0.18f), 2.0f * layout.scale);
        app.renderer.BorderRect(band, app.theme.accent.WithAlpha(0.7f), 1.0f, 2.0f * layout.scale);
    }

    DrawFilterBar(app, layout);
    DrawInspector(app, layout);
    DrawStatusBar(app, layout);

    // Last, so they cover everything and their click targets are the first ones
    // HitTest finds (it walks the list backwards). At most one is open.
    GroupMenuDraw(app, layout, app.palette);
    GroupMenuDraw(app, layout, app.goTo);
    // Il pannello dei valori sta attaccato all'intestazione da cui e' nato, e si
    // disegna per ultimo: e' l'unico che deve coprire la lista.
    GroupMenuDraw(app, layout, app.columnMenu);
    // Le opzioni stanno sopra tutto il resto tranne il proprio sottoelenco: un
    // controllo `Choice` aperto e' l'ultima cosa su cui si sta agendo.
    GroupMenuDraw(app, layout, app.options);
    GroupMenuDraw(app, layout, app.optionsChoice);
    ContextMenuDraw(app, layout);

    // Il suggerimento viene dopo tutto, ma non prende clic: non e' un menu.
    DrawTooltip(app, layout);
}

// Un fotogramma, disegnato adesso.
//
// Il ciclo principale disegna appena puo', ma durante un trascinamento che nasce
// da qui il ciclo e' fermo dentro `DoDragDrop`: la finestra resta com'e' e
// l'evidenziazione della destinazione non comparirebbe mai. Il ciclo modale di OLE
// i messaggi li consegna — e' l'unico motivo per cui la finestra resta viva — e
// quindi basta disegnare quando la destinazione cambia (vedi `DragDrop::Highlight`).
//
// E' l'unico posto in cui si disegna fuori dal ciclo, e ci si arriva solo da un
// callback di OLE, cioe' mai dentro un fotogramma gia' in corso.
void RenderFrame(AppState& app)
{
    if (app.deviceContext == nullptr || app.viewportWidth <= 0 || app.viewportHeight <= 0)
        return;

    glViewport(0, 0, app.viewportWidth, app.viewportHeight);
    glClearColor(0.106f, 0.110f, 0.122f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Le anteprime pronte si caricano prima del fotogramma che le disegna: e' il
    // solo punto in cui l'atlante viene toccato, e gira sul thread che possiede il
    // contesto OpenGL.
    app.previews.Pump();

    app.renderer.Begin(app.viewportWidth, app.viewportHeight);
    app.renderer.ReserveQuads(app.entries.size() * 8 + 512);
    DrawFrame(app, ComputeLayout(app));
    app.renderer.End();

    SwapBuffers(app.deviceContext);
}

// --- input -----------------------------------------------------------------
const ClickTarget* HitTest(const AppState& app, float x, float y, ClickTarget::Kind kind)
{
    // Reverse order: later targets are drawn on top.
    for (auto it = app.clickTargets.rbegin(); it != app.clickTargets.rend(); ++it)
    {
        if (it->kind == kind && it->rect.Contains(x, y))
            return &*it;
    }
    return nullptr;
}

// A click inside an open group menu. The caller turns the action into whatever
// the menu means: running a command, or going to a location.
MenuAction GroupMenuClick(const AppState& app, const Layout& layout, GroupMenu& menu, float x, float y)
{
    if (!menu.open)
        return MenuAction::None;

    if (const ClickTarget* header = HitTest(app, x, y, ClickTarget::Kind::MenuHeader))
    {
        if (menu.collapsed.count(header->label) != 0)
            menu.collapsed.erase(header->label);
        else
            menu.collapsed.insert(header->label);
        menu.dirty = true;
        return MenuAction::None;
    }

    if (const ClickTarget* row = HitTest(app, x, y, ClickTarget::Kind::MenuRow))
    {
        for (int i = 0; i < static_cast<int>(menu.rows.size()); ++i)
        {
            const GroupMenuRow& candidate = menu.rows[static_cast<size_t>(i)];
            if (!candidate.header && candidate.item == row->index)
            {
                menu.selected = i;
                break;
            }
        }
        return MenuAction::Activate;
    }

    // Inside the panel but not on a row: swallow the click. Outside it, close.
    return MenuPanel(app, layout, menu).Contains(x, y) ? MenuAction::None : MenuAction::Close;
}

std::wstring ParentOf(const std::wstring& path)
{
    if (path.size() <= 3)
        return path;   // already a drive root such as C: followed by one slash
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash < 2)
        return path;
    std::wstring parent = path.substr(0, slash);
    if (parent.size() == 2 && parent[1] == L':')
        parent += L"\\";
    return parent;
}

// --- selection --------------------------------------------------------------
int FindEntryByName(const AppState& app, const std::wstring& name)
{
    for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
    {
        if (app.entries[static_cast<size_t>(i)].wideName == name)
            return i;
    }
    return -1;
}

int CursorIndex(const AppState& app)
{
    return app.cursor.empty() ? -1 : FindEntryByName(app, app.cursor);
}

bool IsEntrySelected(const AppState& app, const Entry& entry)
{
    return app.selection.count(entry.wideName) != 0;
}

void SelectOnly(AppState& app, int entryIndex)
{
    app.selection.clear();
    if (entryIndex < 0)
        return;
    const std::wstring& name = app.entries[static_cast<size_t>(entryIndex)].wideName;
    app.selection.insert(name);
    app.cursor          = name;
    app.selectionAnchor = name;
}

void ToggleSelection(AppState& app, int entryIndex)
{
    if (entryIndex < 0)
        return;
    const std::wstring& name = app.entries[static_cast<size_t>(entryIndex)].wideName;
    if (app.selection.count(name) != 0)
        app.selection.erase(name);
    else
        app.selection.insert(name);
    app.cursor          = name;
    app.selectionAnchor = name;
}

// Row index of an entry in the current view, or -1 when the view hides it.
int RowOfEntry(const AppState& app, int entryIndex)
{
    for (int i = 0; i < static_cast<int>(app.rows.size()); ++i)
    {
        if (!app.rows[static_cast<size_t>(i)].header && app.rows[static_cast<size_t>(i)].entry == entryIndex)
            return i;
    }
    return -1;
}

// Adds every entry row between two row indices, inclusive. Group bands between
// them are stepped over rather than treated as boundaries.
void SelectRowRange(AppState& app, int rowFrom, int rowTo)
{
    if (rowFrom < 0 || rowTo < 0)
        return;
    const int low  = (std::min)(rowFrom, rowTo);
    const int high = (std::max)(rowFrom, rowTo);
    for (int i = low; i <= high && i < static_cast<int>(app.rows.size()); ++i)
    {
        const Row& row = app.rows[static_cast<size_t>(i)];
        if (!row.header)
            app.selection.insert(app.entries[static_cast<size_t>(row.entry)].wideName);
    }
}

bool RectsIntersect(const Rect& rect, float x0, float y0, float x1, float y1)
{
    return !(rect.right() < x0 || rect.x > x1 || rect.bottom() < y0 || rect.y > y1);
}

// Ends a rubber band drag. Hit testing uses the row rectangles recorded by the
// last frame, which is at most one frame stale and only matters if the user
// releases in the same instant the list scrolls.
void FinishRectSelection(AppState& app)
{
    if (!app.rectSelecting)
        return;
    app.rectSelecting = false;

    if (!app.rectAdditive)
        app.selection.clear();

    const float x0 = (std::min)(app.rectX0, app.rectX1);
    const float x1 = (std::max)(app.rectX0, app.rectX1);
    const float y0 = (std::min)(app.rectY0, app.rectY1);
    const float y1 = (std::max)(app.rectY0, app.rectY1);

    std::wstring last;
    for (const ClickTarget& target : app.clickTargets)
    {
        if (target.kind != ClickTarget::Kind::Row)
            continue;
        if (!RectsIntersect(target.rect, x0, y0, x1, y1))
            continue;
        const std::wstring& name = app.entries[static_cast<size_t>(target.index)].wideName;
        app.selection.insert(name);
        last = name;
    }

    if (!last.empty())
    {
        app.cursor          = last;
        app.selectionAnchor = last;
    }
}

void ActivateSelection(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    if (cursorIndex < 0)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
    const std::wstring full = EntryLocation(app, entry);
    if (entry.isDirectory && entry.navigable)
    {
        LoadDirectory(app, full);
    }
    else if (entry.isDirectory)
    {
        // Dentro il cestino il nome di una cartella e' il percorso del file
        // riciclato: aprirlo mostrerebbe il blob, non quello che si vede. Li' una
        // cartella si ripristina.
        ReportStatus(app, L"\"" + entry.wideName + L"\" non si apre da qui: si ripristina.");
    }
    else if (!entry.location.empty())
    {
        // Un file dentro il cestino non si apre: si ripristina o si elimina, e
        // quei comandi sono del menu contestuale della shell. Dirlo e' meglio che
        // far finta di averlo aperto.
        ReportStatus(app, L"\"" + entry.wideName + L"\" non si apre da qui: usa il menu contestuale.");
    }
    else
    {
        ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void ScrollBy(AppState& app, const Layout& layout, float delta)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const float content = ViewContentHeight(app, layout);
    const float maxScroll = (std::max)(0.0f, content - layout.list.h);
    app.scroll = (std::min)(maxScroll, (std::max)(0.0f, app.scroll + delta));
}

// Moves the cursor by |delta| entry rows, skipping group bands. Returns without
// moving when there is no further entry in that direction. |extend| is the Shift
// case: the selection becomes the range from the anchor to the new cursor.
void MoveSelection(AppState& app, int delta, bool extend)
{
    const int count = static_cast<int>(app.rows.size());
    if (count == 0 || delta == 0)
        return;

    const int cursorIndex = CursorIndex(app);
    const int position = (cursorIndex < 0) ? -1 : RowOfEntry(app, cursorIndex);

    int target = (position < 0) ? (delta > 0 ? 0 : count - 1) : position + delta;
    const int step = (delta > 0) ? 1 : -1;
    while (target >= 0 && target < count && app.rows[static_cast<size_t>(target)].header)
        target += step;

    if (target < 0 || target >= count)
        return;

    const std::wstring name = app.entries[static_cast<size_t>(app.rows[static_cast<size_t>(target)].entry)].wideName;

    if (extend && !app.selectionAnchor.empty())
    {
        const int anchorRow = RowOfEntry(app, FindEntryByName(app, app.selectionAnchor));
        if (anchorRow >= 0)
        {
            // Rebuilt from the anchor each time, so dragging the cursor back
            // towards the anchor shrinks the range instead of only growing it.
            app.selection.clear();
            SelectRowRange(app, anchorRow, target);
        }
        else
        {
            app.selection.insert(name);
        }
    }
    else
    {
        app.selection.clear();
        app.selection.insert(name);
        app.selectionAnchor = name;
    }

    app.cursor = name;
}

// Keyboard navigation must not be able to walk the selection off the visible
// area; with grouping and filtering the row index of an entry is not its index
// in the directory any more.
void EnsureSelectionVisible(AppState& app, const Layout& layout)
{
    const int cursorIndex = CursorIndex(app);
    if (cursorIndex < 0)
        return;
    const int rowIndex = RowOfEntry(app, cursorIndex);
    if (rowIndex < 0)
        return;

    // ItemRect already accounts for the grid flow and the current scroll, so the
    // correction is just how far the item sticks out.
    const Rect rect = ItemRect(app, layout, rowIndex, app.scroll);
    if (rect.y < layout.list.y)
        app.scroll -= layout.list.y - rect.y;
    else if (rect.bottom() > layout.list.bottom())
        app.scroll += rect.bottom() - layout.list.bottom();
    app.scroll = (std::max)(0.0f, app.scroll);
}

// Apre il menu contestuale da tastiera, ancorato alla riga del cursore. Il tasto
// Menu — o Shift+F10, come in Esplora risorse — deve fare la stessa cosa del
// tasto destro sul file scelto: senza mouse il menu resterebbe irraggiungibile.
// Nel binario originale questa chiave esiste, `HK_ContextMenuOpen`.
void OpenContextMenuAtCursor(AppState& app, const Layout& layout)
{
    const int cursorIndex = CursorIndex(app);
    const int rowIndex    = (cursorIndex < 0) ? -1 : RowOfEntry(app, cursorIndex);
    if (rowIndex < 0)
        return;

    const Rect item = ItemRect(app, layout, rowIndex, app.scroll);
    BuildContextMenu(app, item.x + 40.0f * layout.scale, item.y + item.h * 0.5f);
}

// --- window plumbing -------------------------------------------------------
AppState* StateFromWindow(HWND window)
{
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    AppState* app = StateFromWindow(window);

    switch (message)
    {
        case WM_NCCALCSIZE:
            // wParam arriva a FALSE, non a TRUE. La condizione `wparam == TRUE`
            // non entrava mai, il caso cadeva su DefWindowProc, e DefWindowProc
            // ricalcola il rettangolo cliente DALLO STILE — che contiene
            // WS_CAPTION — quindi rimetteva la didascalia di sistema, disegnata
            // sopra il nostro contenuto. Da qui le due barre.
            //
            // L'area non-cliente va tolta in entrambi i casi.
            if (wparam == TRUE)
            {
                // A finestra ingrandita il sistema gonfia comunque la finestra
                // del bordo di ridimensionamento: il contenuto va rientrato di
                // altrettanto o finirebbe sotto il bordo dello schermo.
                if (IsZoomed(window))
                {
                    NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                    const int frameX = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    const int frameY = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left   += frameX;
                    params->rgrc[0].right  -= frameX;
                    params->rgrc[0].top    += frameY;
                    params->rgrc[0].bottom -= frameY;
                }
            }
            return 0;

        case WM_NCHITTEST:
        {
            if (app == nullptr)
                break;

            POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &screen);
            const float x = static_cast<float>(screen.x);
            const float y = static_cast<float>(screen.y);
            const Layout layout = ComputeLayout(*app);

            if (IsZoomed(window) == FALSE)
            {
                const float border = 6.0f * layout.scale;
                const float width  = static_cast<float>(app->viewportWidth);
                const float height = static_cast<float>(app->viewportHeight);
                const bool left = x < border, right = x >= width - border;
                const bool top  = y < border, bottom = y >= height - border;

                if (top && left)      return HTTOPLEFT;
                if (top && right)     return HTTOPRIGHT;
                if (bottom && left)   return HTBOTTOMLEFT;
                if (bottom && right)  return HTBOTTOMRIGHT;
                if (left)             return HTLEFT;
                if (right)            return HTRIGHT;
                if (top)              return HTTOP;
                if (bottom)           return HTBOTTOM;
            }

            if (y < layout.titleBar.bottom())
            {
                // Buttons and tabs must stay clickable, so they report as client
                // area; the rest of the strip is the drag handle, which also gives
                // double-click to maximise and snap for free.
                for (const ClickTarget& target : app->clickTargets)
                {
                    switch (target.kind)
                    {
                        case ClickTarget::Kind::Menu:
                        case ClickTarget::Kind::Tab:
                        case ClickTarget::Kind::TabClose:
                        case ClickTarget::Kind::TabNew:
                        case ClickTarget::Kind::WindowMinimize:
                        case ClickTarget::Kind::WindowMaximize:
                        case ClickTarget::Kind::WindowClose:
                            if (target.rect.Contains(x, y))
                                return HTCLIENT;
                            break;
                        default:
                            break;
                    }
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_MOUSEWHEEL:
            if (app != nullptr)
            {
                const Layout layout = ComputeLayout(*app);

                // Con il menu aperto la rotella scorre il menu: e' l'unica cosa
                // su cui si sta agendo, e la lista sotto e' coperta. Scorre il
                // livello su cui si sta agendo, che e' il piu' profondo aperto.
                if (app->contextMenu.open)
                {
                    const int depth = ActiveDepth(app->contextMenu);
                    const int steps = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                    app->contextMenu.levels[static_cast<size_t>(depth)].firstItem -= steps;
                    ContextMenuClampWindow(*app, layout, depth, /*followSelection=*/false);
                    return 0;
                }

                // La stessa cosa per una lista a gruppi: la rotella muove il
                // cursore invece di scorrere la lista sotto, che e' coperta.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    // Rotella in su (delta positivo) muove il cursore in su.
                    const int steps = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                    GroupMenuMove(*menu, -steps);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }

                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;

                // Sopra il testo la rotella scorre il testo, e con Ctrl lo ingrandisce:
                // e' il gesto di chi sta leggendo, ed e' l'unico modo di arrivare in
                // fondo a un file lungo senza toccare la lista sotto.
                POINT pointer{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &pointer);
                if (app->textDocument.isText &&
                    app->textViewerRect.Contains(static_cast<float>(pointer.x), static_cast<float>(pointer.y)))
                {
                    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        RunCommand(*app, steps > 0.0f ? CmdTextZoomIn : CmdTextZoomOut);
                    else
                    {
                        const float lineHeight = (app->monoFontOk ? app->monoFont : app->smallFont)
                                                     .LineHeight() * layout.scale * app->textZoom;
                        app->textScroll -= steps * 3.0f * lineHeight;
                    }
                    return 0;
                }

                const ViewMetrics metrics = ComputeViewMetrics(*app, layout);

                // Ctrl+rotella sopra la griglia: la cella si ingrandisce e si
                // stringe di continuo. Il nome della vista segue la misura, cosi'
                // il pie' di pagina e la palette dicono quello che si sta guardando
                // invece di quello che si e' chiuso l'ultima volta.
                if (metrics.grid && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    const float wanted = (std::max)(AppState::kGridIconMin,
                                                    (std::min)(AppState::kGridIconMax,
                                                               app->gridIconSize + steps * 16.0f));
                    if (wanted != app->gridIconSize)
                    {
                        app->gridIconSize = wanted;
                        app->viewMode = wanted <= AppState::kGridIconSplit ? ViewMode::Icons
                                                                          : ViewMode::Tiles;
                        app->viewDirty = true;
                    }
                    return 0;
                }

                const float stride = metrics.grid ? metrics.cellHeight : metrics.rowHeight;
                ScrollBy(*app, layout, -steps * 3.0f * stride);
            }
            return 0;

        case WM_MOUSELEAVE:
            if (app != nullptr)
            {
                app->hovered       = -1;
                app->hoveredPlace  = -1;
                app->hoveredTab    = -1;
                app->hoveredChrome = -1;
            }
            return 0;

        case WM_RBUTTONUP:
        {
            if (app == nullptr)
                break;

            // Con un pannello a gruppi aperto il tasto destro non apre niente: la
            // finestra sotto e' coperta, e un menu contestuale dietro un pannello
            // non si potrebbe ne' vedere ne' usare.
            if (OpenMenu(*app) != nullptr)
                return 0;

            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));

            // Aprire il menu su una riga non selezionata sposta prima la
            // selezione li': il menu descrive cio' su cui si e' cliccato, non
            // quello che era selezionato prima.
            if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
            {
                if (app->selection.count(app->entries[static_cast<size_t>(row->index)].wideName) == 0)
                    SelectOnly(*app, row->index);
            }
            // Sul fondo della lista non c'e' niente su cui agire, quindi la
            // selezione cade e il menu diventa quello della **cartella** — come
            // in Esplora risorse. Il cursore resta: e' il fuoco della tastiera,
            // non una scelta, e cosi' `Menu` continua a funzionare anche qui.
            // Fuori dalla lista (barra laterale, inspector) non si tocca niente:
            // li' un "fondo" non esiste.
            else if (ComputeLayout(*app).list.Contains(x, y))
            {
                app->selection.clear();
            }

            BuildContextMenu(*app, x, y);
            return 0;
        }

        case WM_MOUSEMOVE:
            // Il trascinamento di un tab: si arma al primo movimento oltre la
            // soglia, e da li' in poi ogni movimento aggiorna dove finirebbe.
            if (app != nullptr)
            {
                const float tx = static_cast<float>(GET_X_LPARAM(lparam));
                const float ty = static_cast<float>(GET_Y_LPARAM(lparam));
                if (app->tabPress >= 0 && app->dragTab < 0 &&
                    std::fabs(tx - app->tabPressX) > 6.0f &&
                    app->tabPress < static_cast<int>(app->tabs.size()))
                {
                    app->dragTab  = app->tabs[static_cast<size_t>(app->tabPress)].id;
                    app->tabPress = -1;
                }
                if (app->dragTab >= 0)
                {
                    UpdateTabDrop(*app, tx, ty);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }

            // Il trascinamento di uno slider delle opzioni: finche' il pulsante e'
            // giu', la posizione del puntatore e' il valore. Se il pulsante e' stato
            // lasciato fuori dalla finestra, il trascinamento finisce qui invece di
            // riprendere al movimento successivo.
            if (app != nullptr && app->optionSliderDrag >= 0)
            {
                if ((wparam & MK_LBUTTON) == 0)
                {
                    app->optionSliderDrag = -1;
                }
                else
                {
                    const Layout layout = ComputeLayout(*app);
                    const float x = static_cast<float>(GET_X_LPARAM(lparam));
                    const Rect panel = MenuPanel(*app, layout, app->options);
                    for (GroupMenuItem& item : app->options.items)
                    {
                        if (item.id != app->optionSliderDrag || item.control != OptionControl::Slider)
                            continue;
                        const float scale  = layout.scale;
                        const float width  = OptionControlWidth(item, scale);
                        const float height = 22.0f * scale;
                        const float left   = panel.right() - 6.0f * scale - width - 14.0f * scale;
                        const float knob   = height * 0.5f;
                        const float track  = (std::max)(1.0f, width - knob * 2.0f);
                        float fraction = (x - (left + knob)) / track;
                        fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
                        item.fraction = fraction;
                        ApplyOptionFromItem(*app, item);
                        break;
                    }
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }

            // Il tempo del sovrappasso riparte quando il puntatore si muove: fermo
            // sulla stessa riga, il conto scorre; in movimento, no.
            if (app != nullptr)
            {
                const float mx = static_cast<float>(GET_X_LPARAM(lparam));
                const float my = static_cast<float>(GET_Y_LPARAM(lparam));
                if (std::fabs(mx - app->pointerX) > 2.0f || std::fabs(my - app->pointerY) > 2.0f)
                {
                    app->hoverStart = NowSeconds();
                    app->pointerX   = mx;
                    app->pointerY   = my;
                }
            }

            // Il trascinamento di un bordo: la colonna cresce e la vicina si
            // stringe, e nessuna delle due scende sotto la larghezza leggibile.
            if (app != nullptr && app->columnDrag > 0)
            {
                const Layout layout = ComputeLayout(*app);
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const int index = app->columnDrag;
                const float moved = app->columnDragStartFraction +
                                    (x - app->columnDragStartX) / (std::max)(1.0f, layout.header.w);
                const float minimum = kMinColumnWidth * layout.scale /
                                      (std::max)(1.0f, layout.header.w);
                const float left  = app->columns[index - 1].fraction + minimum;
                const float right = (index + 1 < kColumnCount ? app->columns[index + 1].fraction
                                                              : 1.0f) - minimum;
                app->columns[index].fraction = (std::min)(right, (std::max)(left, moved));
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));

                // Uscendo dalla finestra il mouse non manda altri WM_MOUSEMOVE,
                // quindi il riquadro di sovrappasso resterebbe appeso sull'ultima
                // riga toccata. TrackMouseEvent fa arrivare un WM_MOUSELEAVE, e va
                // richiesto a ogni movimento perche' la richiesta vale una volta
                // sola.
                TRACKMOUSEEVENT track{};
                track.cbSize    = sizeof(track);
                track.dwFlags   = TME_LEAVE;
                track.hwndTrack = window;
                TrackMouseEvent(&track);

                if (app->rectSelecting)
                {
                    app->rectX1 = x;
                    app->rectY1 = y;
                    return 0;
                }

                // Trascinamento armato e mouse che si allontana: si parte. La
                // soglia e' quella di sistema (`SM_CXDRAG`), perche' una mano che
                // trema su un clic non deve spostare dei file.
                if (app->dragArmed && (GetKeyState(VK_LBUTTON) & 0x8000) != 0)
                {
                    const float farX = std::fabs(x - app->dragStartX);
                    const float farY = std::fabs(y - app->dragStartY);
                    const float thresholdX = static_cast<float>(GetSystemMetrics(SM_CXDRAG));
                    const float thresholdY = static_cast<float>(GetSystemMetrics(SM_CYDRAG));
                    if (farX > thresholdX || farY > thresholdY)
                    {
                        app->dragArmed = false;   // si parte una volta sola

                        const std::vector<std::wstring> paths = SelectedPaths(*app);
                        DWORD effect = DROPEFFECT_NONE;
                        size_t dragged = paths.size();
                        app->dropReported = false;

                        // Da una cartella che non e' un percorso si trascina
                        // **l'oggetto della shell**, non un nome: il nome di una voce
                        // del cestino e' il percorso del file dentro `$Recycle.Bin`, e
                        // trascinare quello significherebbe trascinare un altro file.
                        if (ShellLocation::IsNamespace(app->directory))
                        {
                            const std::vector<const BYTE*> pidls = SelectedPidls(*app);
                            dragged = pidls.size();
                            app->draggingNamespaceItems = true;
                            if (IDataObject* data = DragDrop::DataObjectForPidls(window, pidls))
                            {
                                DragDrop::DragOutData(window, data, pidls.size(), &effect);
                                data->Release();
                            }
                            else
                            {
                                ReportStatus(*app, L"La shell non sa preparare queste voci per il "
                                                   L"trascinamento: si gestiscono col menu "
                                                   L"contestuale.");
                            }
                            app->draggingNamespaceItems = false;
                        }
                        else
                        {
                            DragDrop::DragOut(window, paths, &effect);
                        }

                        // Spostati davvero: gli originali non ci sono piu' e la
                        // vista li sta ancora mostrando.
                        if ((effect & DROPEFFECT_MOVE) != 0 && dragged > 0)
                        {
                            ReloadKeeping(*app, /*keep=*/{}, /*preferCursor=*/{}, app->scroll);
                            // Il messaggio lo da' chi ha eseguito la caduta, se e'
                            // stato uno di noi: sa dove sono finiti i file, e
                            // "Spostati 3 elementi" senza dire dove e' meno di
                            // "Buttati via 3 elementi nel cestino". Qui si parla
                            // solo se la destinazione era un'altra applicazione,
                            // che non ha modo di dirlo.
                            if (!app->dropReported)
                                ReportStatus(*app, L"Spostati " + std::to_wstring(dragged) +
                                                       L" elementi");
                        }
                        return 0;
                    }
                }

                // Con il menu contestuale aperto il puntatore sceglie la voce, non
                // la riga sotto: la lista e' coperta, e sovrappassarla sarebbe un
                // secondo stato che l'utente non vede.
                if (app->contextMenu.open)
                {
                    const Layout layout = ComputeLayout(*app);
                    ContextMenuHover(*app, layout, x, y);
                    app->hovered = -1;
                    return 0;
                }

                app->hovered = -1;
                app->hoveredPlace = -1;
                app->hoveredTab = -1;
                app->hoveredChrome = -1;
                app->hoveredSection = -1;
                app->hoveredNav = -1;

                if (const ClickTarget* nav = HitTest(*app, x, y, ClickTarget::Kind::NavButton))
                    app->hoveredNav = nav->index;
                if (HitTest(*app, x, y, ClickTarget::Kind::Menu))              app->hoveredChrome = 0;
                else if (HitTest(*app, x, y, ClickTarget::Kind::TabNew))       app->hoveredChrome = 1;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMinimize)) app->hoveredChrome = 2;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMaximize)) app->hoveredChrome = 3;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowClose))    app->hoveredChrome = 4;

                const int wasHovered = app->hovered;
                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                    app->hoveredTab = tab->index;
                else if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                    app->hovered = row->index;
                if (app->hovered != wasHovered)
                    app->hoverStart = NowSeconds();
                else if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
                    app->hoveredPlace = place->index;
                else if (const ClickTarget* header = HitTest(*app, x, y, ClickTarget::Kind::SidebarHeader))
                    app->hoveredSection = static_cast<int>(header->rect.y);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));

                // Un bordo d'intestazione si prende e si trascina. La presa viene
                // prima di tutto il resto: mentre si trascina, la lista sotto non
                // deve rispondere ai clic.
                if (const ClickTarget* edge = HitTest(*app, x, y, ClickTarget::Kind::ColumnEdge))
                {
                    app->columnDrag = edge->index;
                    app->columnDragStartX = x;
                    app->columnDragStartFraction = app->columns[edge->index].fraction;
                    SetCapture(window);
                    return 0;
                }

                const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                // Il menu contestuale e' modale quanto la palette: finche' e'
                // aperto prende il puntatore e la tastiera, e la lista sotto non
                // risponde.
                if (app->contextMenu.open)
                {
                    const Layout layout = ComputeLayout(*app);
                    if (ContextMenuClick(*app, layout, x, y))
                        return 0;
                }

                // An open menu is modal: its clicks are handled first and nothing
                // underneath is reachable.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    const Layout layout = ComputeLayout(*app);
                    // Il pannello delle opzioni calcola da se' la riga e il
                    // controllo sotto il puntatore: non lascia bersagli nella
                    // lista, perche' la geometria ha una sola definizione.
                    MenuAction action = (menu == &app->options)
                                            ? OptionsClick(*app, layout, *menu, x, y)
                                            : GroupMenuClick(*app, layout, *menu, x, y);
                    if (action == MenuAction::Close)
                    {
                        // Chiudere il pannello mentre si scrive in un campo non deve
                        // lasciare la scrittura appesa: senza questo, il primo
                        // carattere digitato dopo sarebbe finito in un campo chiuso.
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        menu->open = false;
                    }
                    else if (action == MenuAction::Activate)
                    {
                        if (menu == &app->palette)
                            RunPaletteSelection(*app);
                        else if (menu == &app->goTo)
                            RunGoToSelection(*app);
                        else if (menu == &app->optionsChoice)
                            RunOptionsChoiceSelection(*app);
                        else
                            RunColumnMenuSelection(*app);
                    }
                    return 0;
                }

                // La linguetta del filtro: apre l'elenco dei valori di quella
                // colonna, o richiude quello che c'e' gia'.
                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::ColumnFilter))
                {
                    if (app->columnPanel == tab->index && app->columnMenu.open)
                    {
                        app->columnMenu.open = false;
                        app->columnPanel     = -1;
                    }
                    else
                    {
                        BuildColumnMenu(*app, tab->index);
                    }
                    return 0;
                }

                if (const ClickTarget* nav = HitTest(*app, x, y, ClickTarget::Kind::NavButton))
                {
                    // I pulsanti non fanno niente di nuovo: eseguono i comandi che
                    // esistono gia', quelli della tastiera e della palette. Una sola
                    // implementazione per ogni azione, o prima o poi divergono.
                    static const int kNavCommands[5] = {CmdBack, CmdForward, CmdGoUp, CmdRefresh,
                                                        CmdToggleBookmark};
                    if (nav->index >= 0 && nav->index < 5)
                        RunCommand(*app, kNavCommands[nav->index]);
                }
                else if (const ClickTarget* closeTab = HitTest(*app, x, y, ClickTarget::Kind::TabClose))
                {
                    CloseTab(*app, closeTab->index);
                }
                else if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                {
                    ActivateTab(*app, tab->index);
                    // Il gesto puo' diventare un trascinamento, ma solo se il mouse
                    // si muove: un clic non deve spostare niente.
                    app->tabPress  = tab->index;
                    app->tabPressX = x;
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::TabNew))
                {
                    NewTab(*app, app->directory.empty() ? L"C:\\" : app->directory);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::Menu))
                {
                    app->sidebarVisible = !app->sidebarVisible;
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMinimize))
                {
                    SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMaximize))
                {
                    SendMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowClose))
                {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                }
                else if (const ClickTarget* box = HitTest(*app, x, y, ClickTarget::Kind::Checkbox))
                {
                    // Sceglie e basta: non apre, non arma un trascinamento, e non
                    // tocca il cursore. E' tutto quello che serve a una casella.
                    ToggleSelection(*app, box->index);
                    return 0;
                }
                else if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                {
                    // La pressione su una riga arma un trascinamento: partira' solo
                    // se il mouse si muove oltre la soglia, e fino ad allora resta
                    // il clic che e' sempre stato.
                    app->dragArmed  = true;
                    app->dragStartX = x;
                    app->dragStartY = y;

                    if (ctrl)
                    {
                        ToggleSelection(*app, row->index);
                    }
                    else if (shift)
                    {
                        // Extend from the anchor to the clicked row, across group
                        // bands, matching what the keyboard does.
                        const int anchorEntry = FindEntryByName(*app, app->selectionAnchor);
                        SelectRowRange(*app, RowOfEntry(*app, anchorEntry), RowOfEntry(*app, row->index));
                        app->cursor = app->entries[static_cast<size_t>(row->index)].wideName;
                    }
                    else
                    {
                        SelectOnly(*app, row->index);
                    }

                    // `ActOnPress`: con il clic singolo acceso, agire *premendo*
                    // invece che mollando. E' l'unica differenza fra le due chiavi.
                    if (app->actOnPress &&
                        SingleClickActivates(*app, app->entries[static_cast<size_t>(row->index)]))
                    {
                        app->dragArmed = false;   // il gesto e' finito qui: non parte un trascinamento
                        ActivateSelection(*app);
                    }
                }
                else if (const ClickTarget* group = HitTest(*app, x, y, ClickTarget::Kind::GroupHeader))
                {
                    // Fold state is keyed by label, so it survives a rebuild of
                    // the view when the statistics change.
                    if (app->collapsedGroups.count(group->label) != 0)
                        app->collapsedGroups.erase(group->label);
                    else
                        app->collapsedGroups.insert(group->label);
                    app->viewDirty = true;
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::GroupMode) != nullptr)
                {
                    app->groupMode = static_cast<GroupMode>((static_cast<int>(app->groupMode) + 1) % 6);
                    app->viewDirty = true;
                }
                else if (const ClickTarget* header = HitTest(*app, x, y, ClickTarget::Kind::Column))
                {
                    const SortColumn column = kColumns[header->index].sort;
                    if (column == app->sortColumn)
                        app->sortAscending = !app->sortAscending;
                    else
                    {
                        app->sortColumn = column;
                        app->sortAscending = true;
                    }
                    SortEntries(*app);
                    app->viewDirty = true;
                }
                else if (const ClickTarget* crumb = HitTest(*app, x, y, ClickTarget::Kind::Breadcrumb))
                {
                    LoadDirectory(*app, crumb->path);
                }
                else if (const ClickTarget* section = HitTest(*app, x, y,
                                                             ClickTarget::Kind::SidebarHeader))
                {
                    // Ricordare la sezione chiusa per nome e non per posizione: le
                    // righe si spostano quando i segnalibri cambiano, e una sezione
                    // chiusa non deve riaprirsi da sola perche' ne e' arrivata
                    // un'altra sopra.
                    if (app->collapsedSections.count(section->label) != 0)
                        app->collapsedSections.erase(section->label);
                    else
                        app->collapsedSections.insert(section->label);
                }
                else if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
                {
                    LoadDirectory(*app, place->path);
                }
                else
                {
                    // Nothing under the cursor: an empty area of the list starts a
                    // rubber band. Everywhere else the press is simply ignored.
                    const Layout layout = ComputeLayout(*app);
                    if (layout.list.Contains(x, y))
                    {
                        app->rectSelecting = true;
                        // Shift adds to the selection here too, the way it does on a
                        // click: a band is a range gesture, and Shift is the range
                        // modifier.
                        app->rectAdditive  = ctrl || shift;
                        app->rectX0 = app->rectX1 = x;
                        app->rectY0 = app->rectY1 = y;
                        if (!ctrl)
                        {
                            app->selection.clear();
                            app->cursor.clear();
                            app->selectionAnchor.clear();
                        }
                    }
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (app != nullptr && app->optionSliderDrag >= 0)
            {
                // Fine del trascinamento di uno slider: il valore e' gia' stato
                // applicato a ogni movimento, qui si lascia solo la presa.
                app->optionSliderDrag = -1;
                return 0;
            }
            if (app != nullptr && app->dragTab >= 0)
            {
                ApplyTabDrop(*app);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (app != nullptr)
                app->tabPress = -1;
            if (app != nullptr && app->columnDrag > 0)
            {
                app->columnDrag = -1;
                ReleaseCapture();
                return 0;
            }
            if (app != nullptr)
            {
                // `SingleClick`: mollare su una riga la apre, se non e' partito un
                // trascinamento. `dragArmed` e' ancora vero solo se il mouse non si
                // e' mosso abbastanza da armarlo — e allora il gesto era un clic,
                // non un trascinamento.
                const bool wasClick = app->dragArmed;
                app->dragArmed = false;

                if (wasClick && app->singleClick && !app->actOnPress)
                {
                    const float x = static_cast<float>(GET_X_LPARAM(lparam));
                    const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                    if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                    {
                        const Entry& entry = app->entries[static_cast<size_t>(row->index)];
                        if (SingleClickActivates(*app, entry))
                            ActivateSelection(*app);
                    }
                }

                FinishRectSelection(*app);
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            // Doppio clic su un bordo: la colonna torna come era nata. E' il modo di
            // disfare un trascinamento senza doverlo indovinare a rovescio.
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                if (const ClickTarget* edge = HitTest(*app, x, y, ClickTarget::Kind::ColumnEdge))
                {
                    app->columns[edge->index].fraction = kColumns[edge->index].fraction;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                {
                    SelectOnly(*app, row->index);
                    ActivateSelection(*app);
                }
                else if (app->doubleClickToParent &&
                         ComputeLayout(*app).list.Contains(x, y))
                {
                    // `DoubleClickToParent`: il doppio clic sul fondo va su di un
                    // livello. E' il gesto che Windows chiama "doppio clic per
                    // salire", e vale solo sul fondo: su una riga il doppio clic
                    // apre, che e' un'altra cosa.
                    RunCommand(*app, CmdGoUp);
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (app != nullptr)
            {
                const Layout layout = ComputeLayout(*app);

                // An open menu is modal, so it takes the keyboard first. Returning
                // here does not stop TranslateMessage from producing WM_CHAR, which
                // is where printable characters are routed to the menu filter.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool shift   = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    // Il campo di testo di un'opzione, mentre si scrive, prende la
                    // tastiera come la rinomina: Backspace cancella una lettera del
                    // nome, non del filtro.
                    if (app->optionEditActive)
                    {
                        switch (wparam)
                        {
                            case VK_RETURN: CommitOptionEdit(*app); break;
                            case VK_ESCAPE: CancelOptionEdit(*app); break;
                            case VK_BACK:
                                if (!app->optionEditText.empty())
                                    app->optionEditText.pop_back();
                                break;
                            default: break;
                        }
                        return 0;
                    }

                    // Ogni menu si richiude col suo tasto. Le scorciatoie sono
                    // quelle del riferimento: Ctrl+P e F4 al GoTo, Ctrl+Maiusc+P ai
                    // comandi, Ctrl+, alle opzioni.
                    const bool toggling = control &&
                        ((menu == &app->goTo    && wparam == 'P' && !shift) ||
                         (menu == &app->palette && wparam == 'P' && shift)  ||
                         (menu == &app->options && wparam == VK_OEM_COMMA));
                    if (toggling)
                    {
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        menu->open = false;
                        return 0;
                    }

                    // I tasti del **controllo** di una riga di opzione: Invio,
                    // Spazio e frecce. Su/Giu'/Home/Fine/Esc restano al componente.
                    if (menu == &app->options && OptionsKeyDown(*app, *menu, wparam))
                        return 0;

                    const MenuAction action = GroupMenuKeyDown(*menu, wparam);
                    if (action == MenuAction::Close)
                    {
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        menu->open = false;
                    }
                    else if (action == MenuAction::Activate)
                    {
                        if (menu == &app->palette)
                            RunPaletteSelection(*app);
                        else if (menu == &app->goTo)
                            RunGoToSelection(*app);
                        else if (menu == &app->optionsChoice)
                            RunOptionsChoiceSelection(*app);
                        else
                            RunColumnMenuSelection(*app);
                    }
                    return 0;
                }

                // Il menu contestuale prende la tastiera prima di tutto il resto,
                // rinomina compresa: finche' e' aperto e' lui la cosa su cui si
                // sta agendo.
                if (app->contextMenu.open)
                {
                    switch (wparam)
                    {
                        // Esc e freccia sinistra risalgono di un livello: dal
                        // primo livello chiudono il menu. E' l'unico modo di
                        // uscire da un sottomenu senza eseguire niente.
                        case VK_ESCAPE:
                        case VK_LEFT:   ContextMenuRaiseLevel(*app);       break;
                        case VK_RIGHT:  ContextMenuDescend(*app, layout);    break;
                        case VK_RETURN: ContextMenuActivate(*app); break;
                        case VK_UP:     ContextMenuMove(*app, layout, -1); break;
                        case VK_DOWN:   ContextMenuMove(*app, layout, 1);  break;
                        // In un menu che scorre, PagSu e PagGiu' scorrono di una
                        // schermata e Home/Fine vanno agli estremi: e' quello che
                        // fanno in ogni menu, e con venti voci la differenza si
                        // sente.
                        case VK_NEXT:   ContextMenuMove(*app, layout, 5);  break;
                        case VK_PRIOR:  ContextMenuMove(*app, layout, -5); break;
                        case VK_HOME:
                        case VK_END:
                            ContextMenuJump(*app, layout, wparam == VK_HOME);
                            break;
                        default: break;
                    }
                    return 0;
                }

                // La rinomina sul posto prende la tastiera prima di tutto il
                // resto. Deve stare qui e non dentro lo switch: con la rinomina
                // attiva, Backspace deve cancellare una lettera del nome e non del
                // filtro, ed Esc deve annullare il nome e non svuotare il filtro.
                if (app->renameActive)
                {
                    switch (wparam)
                    {
                        case VK_RETURN: CommitRename(*app); break;
                        case VK_ESCAPE: CancelRename(*app); break;
                        case VK_BACK:
                            if (!app->renameText.empty())
                                app->renameText.pop_back();
                            break;
                        default:
                            break;
                    }
                    return 0;
                }

                switch (wparam)
                {
                    case VK_ESCAPE:
                        // Escape unwinds the filter and stops there. Closing the
                        // window is the ✕ button or Alt+F4: an application that
                        // exits on a bare Escape is a trap, and this one did.
                        if (!app->filter.empty() || app->filterActive)
                        {
                            app->filter.clear();
                            app->filterActive = false;
                            app->viewDirty = true;
                        }
                        break;
                    case VK_DOWN:
                    case VK_UP:
                    case VK_NEXT:
                    case VK_PRIOR:
                    {
                        const bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        const ViewMetrics metrics = ComputeViewMetrics(*app, layout);
                        const float stride = metrics.grid ? metrics.cellHeight : metrics.rowHeight;
                        const int step = metrics.grid ? metrics.columns : 1;
                        const int page = (std::max)(1, static_cast<int>(layout.list.h / stride)) * step;

                        int delta = step;
                        if (wparam == VK_UP)         delta = -step;
                        else if (wparam == VK_NEXT)  delta = page;
                        else if (wparam == VK_PRIOR) delta = -page;

                        MoveSelection(*app, delta, extend);
                        EnsureSelectionVisible(*app, layout);
                        break;
                    }
                    case VK_LEFT:
                    case VK_RIGHT:
                    {
                        // Only a grid has a horizontal neighbour to move to.
                        const ViewMetrics metrics = ComputeViewMetrics(*app, layout);
                        if (!metrics.grid)
                            break;
                        const bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        MoveSelection(*app, wparam == VK_LEFT ? -1 : 1, extend);
                        EnsureSelectionVisible(*app, layout);
                        break;
                    }
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            app->viewMode = (wparam == '1') ? ViewMode::Details
                                          : (wparam == '2') ? ViewMode::List
                                          : (wparam == '3') ? ViewMode::Tiles
                                                            : ViewMode::Icons;
                            app->viewDirty = true;
                        }
                        break;
                    case VK_RETURN:
                        ActivateSelection(*app);
                        break;
                    case VK_BACK:
                        // Backspace edits the filter while it is active, and only
                        // navigates to the parent when there is nothing to erase.
                        if (app->filterActive && !app->filter.empty())
                        {
                            app->filter.pop_back();
                            app->viewDirty = true;
                        }
                        else
                        {
                            LoadDirectory(*app, ParentOf(app->directory));
                        }
                        break;
                    case VK_F5:
                        // A refresh means the tree may have changed: drop the
                        // cached folder statistics so they are counted again.
                        app->stats.ClearAll();
                        LoadDirectory(*app, app->directory);
                        break;
                    case VK_APPS:
                        // Il tasto Menu: aprire il menu contestuale e' l'unica
                        // cosa che possa significare.
                        OpenContextMenuAtCursor(*app, layout);
                        break;
                    case VK_HOME:
                    {
                        app->scroll = 0.0f;
                        for (const Row& row : app->rows)
                        {
                            if (!row.header)
                            {
                                SelectOnly(*app, row.entry);
                                break;
                            }
                        }
                        break;
                    }
                    case VK_OEM_PLUS:
                    case VK_ADD:
                        // Zoom moved to Ctrl+plus so that a plain '+' can be typed
                        // into the filter, which is what CharacterKeyAction's
                        // TypeInFilter mode requires.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            app->zoom = (std::min)(2.0f, app->zoom + 0.1f);
                        break;
                    case VK_OEM_MINUS:
                    case VK_SUBTRACT:
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            app->zoom = (std::max)(0.6f, app->zoom - 0.1f);
                        break;

                    case 'A':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            // Selects what is on screen: the filter is part of the
                            // question, so it is part of the answer.
                            app->selection.clear();
                            for (const Row& row : app->rows)
                            {
                                if (!row.header)
                                    app->selection.insert(app->entries[static_cast<size_t>(row.entry)].wideName);
                            }
                        }
                        break;
                    case 'C':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            DoCopyToClipboard(*app, /*cut=*/false);
                        break;
                    case 'X':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            DoCopyToClipboard(*app, /*cut=*/true);
                        break;
                    case 'V':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            DoPaste(*app);
                        break;
                    case 'N':
                        // Ctrl+Shift+N, come Esplora risorse. Ctrl+N da solo resta
                        // libero: non c'e' ancora una finestra nuova da aprire.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                            (GetKeyState(VK_SHIFT) & 0x8000) != 0)
                            DoNewFolder(*app);
                        break;
                    case VK_F2:
                        BeginRename(*app);
                        break;
                    case VK_DELETE:
                    {
                        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        DoDelete(*app, shift);
                        break;
                    }
                    case 'T':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            NewTab(*app, app->directory.empty() ? L"C:\\" : app->directory);
                        break;
                    case 'P':
                        // Le scorciatoie del riferimento: Ctrl+Maiusc+P apre i
                        // comandi, Ctrl+P va a una cartella. Erano scambiate.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                                BuildPaletteMenu(*app);
                            else
                                BuildGoToMenu(*app);
                        }
                        break;
                    case VK_F4:
                        // F4 da solo: Alt+F4 resta la chiusura della finestra, e
                        // arriva come WM_SYSKEYDOWN, quindi non passa di qui.
                        if ((GetKeyState(VK_MENU) & 0x8000) == 0)
                            BuildGoToMenu(*app);
                        break;
                    case VK_OEM_COMMA:
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            BuildOptionsMenu(*app);
                        break;
                    case 'G':
                        // GoTo ha preso Ctrl+P e F4, come il riferimento. Ctrl+G
                        // resta libero: ciclare il raggruppamento sta su
                        // Ctrl+Maiusc+G, che e' l'unico uso rimasto.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                            (GetKeyState(VK_SHIFT) & 0x8000) != 0)
                        {
                            app->groupMode = static_cast<GroupMode>((static_cast<int>(app->groupMode) + 1) % 6);
                            app->viewDirty = true;
                        }
                        break;
                    case 'W':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            CloseTab(*app, app->activeTab);
                        break;
                    case VK_TAB:
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            const int count = static_cast<int>(app->tabs.size());
                            if (count > 1)
                                ActivateTab(*app, (app->activeTab + 1) % count);
                        }
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case WM_CHAR:
            if (app != nullptr)
            {
                // CharacterKeyAction defaults to typing into the filter rather
                // than triggering a command, so a printable character over the
                // list starts filtering. Backspace is handled in WM_KEYDOWN: not
                // handling it here avoids erasing two characters per press.
                const wchar_t character = static_cast<wchar_t>(wparam);
                if (character >= 0x20 && character != 0x7F)
                {
                    if (app->renameActive)
                    {
                        // I caratteri non validi in un nome di file li rifiuta
                        // IFileOperation con un messaggio: meglio non anticiparli,
                        // cosi' l'errore lo spiega il sistema.
                        const std::wstring wide(1, character);
                        app->renameText += ToUtf8(wide);
                    }
                    else if (app->optionEditActive)
                    {
                        // Il campo di testo di un'opzione: il carattere va nel
                        // nome, non nel filtro del pannello che lo contiene.
                        const std::wstring wide(1, character);
                        app->optionEditText += ToUtf8(wide);
                    }
                    else if (GroupMenu* menu = OpenMenu(*app))
                    {
                        GroupMenuChar(*menu, character);
                    }
                    else
                    {
                        app->filter.push_back(character);
                        app->filterActive = true;
                        app->viewDirty = true;
                        app->scroll = 0.0f;
                    }
                }
            }
            return 0;

        case WM_SYSKEYDOWN:
            // Shift+F10 e' un tasto di sistema come Alt+freccia: F10 da solo
            // attiva il menu della finestra, quindi non passa da WM_KEYDOWN.
            if (app != nullptr && wparam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                OpenContextMenuAtCursor(*app, ComputeLayout(*app));
                return 0;
            }

            // Alt+Left and Alt+Right arrive as system keys, not as ordinary ones,
            // because the window has a system menu.
            if (app != nullptr && wparam == VK_LEFT)
            {
                NavigateBack(*app);
                return 0;
            }
            if (app != nullptr && wparam == VK_RIGHT)
            {
                NavigateForward(*app);
                return 0;
            }
            break;

        case WM_SETCURSOR:
            // Su un bordo d'intestazione il puntatore dice che li' si trascina: senza
            // questo, la striscia di presa esiste e non si vede.
            if (app != nullptr && LOWORD(lparam) == HTCLIENT)
            {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                const Layout layout = ComputeLayout(*app);
                if (layout.header.Contains(static_cast<float>(point.x), static_cast<float>(point.y)) &&
                    HitTest(*app, static_cast<float>(point.x), static_cast<float>(point.y),
                            ClickTarget::Kind::ColumnEdge) != nullptr)
                {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;

        case WM_SIZE:
            if (app != nullptr)
            {
                app->viewportWidth  = LOWORD(lparam);
                app->viewportHeight = HIWORD(lparam);
            }
            return 0;

        case WM_DESTROY:
            // La finestra sta per morire: la registrazione come destinazione di
            // caduta va tolta adesso, o OLE resta con un puntatore a una finestra
            // che non c'e' piu'.
            DragDrop::Unregister(window);

            // Uscendo con il menu aperto la shell ci ha prestato un oggetto che
            // nessuno rilascerebbe piu'.
            if (app != nullptr)
                ContextMenuClose(*app);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

bool CreateOpenGLContext(HWND window, HDC* outDc, HGLRC* outContext)
{
    HDC dc = GetDC(window);
    if (dc == nullptr)
        return false;

    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize      = sizeof(descriptor);
    descriptor.nVersion   = 1;
    descriptor.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cAlphaBits = 8;
    descriptor.cDepthBits = 24;
    descriptor.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(dc, &descriptor);
    if (format == 0 || SetPixelFormat(dc, format, &descriptor) == FALSE)
    {
        ReleaseDC(window, dc);
        return false;
    }

    HGLRC temporary = wglCreateContext(dc);
    if (temporary == nullptr)
    {
        ReleaseDC(window, dc);
        return false;
    }
    wglMakeCurrent(dc, temporary);

    HGLRC context = nullptr;
    auto createAttribs = reinterpret_cast<WglCreateContextAttribsArbFn>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (createAttribs != nullptr)
    {
        const int attributes[] = {
            kWglContextMajorVersion, 3,
            kWglContextMinorVersion, 3,
            kWglContextProfileMask,  kWglContextCoreProfile,
            0, 0};
        context = createAttribs(dc, nullptr, attributes);
    }

    if (context == nullptr)
        context = wglCreateContext(dc);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temporary);

    if (context == nullptr || wglMakeCurrent(dc, context) == FALSE)
    {
        ReleaseDC(window, dc);
        return false;
    }

    *outDc      = dc;
    *outContext = context;
    return true;
}

void EnableHighDpi()
{
    using SetDpiAwarenessFn = BOOL(WINAPI*)(void*);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32 != nullptr)
    {
        auto setContext = reinterpret_cast<SetDpiAwarenessFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext != nullptr && setContext(reinterpret_cast<void*>(-4)) != FALSE)
            return;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        FreeLibrary(user32);
    }
    SetProcessDPIAware();
}

// Writes a startup failure next to the executable. A MessageBox is useless when
// the program is launched by a script or a capture loop, and this message is the
// only diagnostic available before the window exists.
void WriteStartupLog(const std::string& message)
{
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return;

    std::wstring file = modulePath;
    const size_t slash = file.find_last_of(L'\\');
    file = (slash == std::wstring::npos) ? L"startup-error.log" : file.substr(0, slash + 1) + L"startup-error.log";

    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(handle, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    CloseHandle(handle);
}

void ShowError(const std::string& message)
{
    WriteStartupLog(message);
    const std::string text = "Bolt\n\n" + message;
    MessageBoxA(nullptr, text.c_str(), "Startup failed", MB_ICONERROR | MB_OK);
}

double NowSeconds()
{
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

} // namespace

// --- trascinamento: la parte che sa la vista -------------------------------
//
// Il modulo OLE (`drag_drop.cpp`) non sa cosa sia una riga o una cartella aperta:
// sa leggere un oggetto dati e consegnare una caduta. La destinazione la decide
// chi conosce l'elenco, cioe' queste tre funzioni.

namespace DragDrop
{

DragDrop::Target TargetAt(HWND window, POINT client)
{
    DragDrop::Target target;
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return target;

    const float x = static_cast<float>(client.x);
    const float y = static_cast<float>(client.y);

    // Un trascinamento nato da una cartella di spazio dei nomi porta oggetti della
    // shell, che non hanno un percorso: metterli "dentro" una cartella nostra non e'
    // una cosa che sappiamo fare. Si trascinano fuori, verso un'altra applicazione,
    // che di quegli oggetti sa cosa fare.
    if (app->draggingNamespaceItems)
        return target;

    // Il riquadro di navigazione. Le sue righe sono gia' bersagli di clic, quindi la
    // geometria la sa `HitTest`: rifarla qui vorrebbe dire sbagliarla appena una
    // sezione si chiude.
    if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
    {
        target.path       = place->path;
        target.sidebarRow = place->index;
        if (!app->recycleBinPath.empty() && place->path == app->recycleBinPath)
            target.verb = DragDrop::DropVerb::Discard;
        else if (!ShellLocation::IsNamespace(place->path))
            target.verb = DragDrop::DropVerb::DropInto;
        // "Questo PC", o un'altra cartella di spazio dei nomi: non si sa dove
        // mettere i file. Rifiutare e' meglio che promettere e non fare.
        return target;
    }

    if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
    {
        const Entry& entry = app->entries[static_cast<size_t>(row->index)];
        if (!entry.isDirectory)
        {
            // Sopra un file la destinazione e' la cartella che lo contiene, come
            // in Esplora risorse: li' un file non riceve niente, ma la lista si'.
            target.path = app->directory;
            target.verb = DragDrop::DropVerb::DropInto;
            return target;
        }
        // Una cartella va aperta per lasciarci qualcosa dentro: la riga mostra il
        // nome, la destinazione e' il percorso.
        target.path    = JoinPath(app->directory, entry.wideName);
        target.verb    = DragDrop::DropVerb::DropInto;
        target.ontoRow = true;
        return target;
    }

    const Layout layout = ComputeLayout(*app);
    if (layout.list.Contains(x, y))
    {
        target.path = app->directory;
        target.verb = DragDrop::DropVerb::DropInto;
    }
    return target;   // fuori dalla lista non si lascia cadere niente
}

void Highlight(HWND window, const DragDrop::Target& target)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return;

    const std::wstring previous = app->dropTarget;
    const bool previousRow      = app->dropTargetIsRow;
    const int  previousSidebar  = app->dropTargetSidebarRow;

    app->dropTarget           = target.path;
    app->dropTargetIsRow      = target.ontoRow && !target.Empty();
    app->dropTargetSidebarRow = target.Empty() ? -1 : target.sidebarRow;

    // L'evidenziazione va vista ADESSO: se il trascinamento e' nato da qui, il
    // ciclo principale e' fermo dentro `DoDragDrop`, e senza questo disegno la
    // riga non si accenderebbe mai. Solo quando cambia: `DragOver` arriva a ogni
    // movimento del mouse, e un fotogramma per movimento sarebbe lavoro sprecato.
    if (app->dropTarget != previous || app->dropTargetIsRow != previousRow ||
        app->dropTargetSidebarRow != previousSidebar)
        RenderFrame(*app);
}

void Drop(HWND window, const std::vector<std::wstring>& sources, const DragDrop::Target& target,
          bool move)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr || sources.empty())
        return;

    const float scroll = app->scroll;
    std::wstring error;
    std::vector<std::wstring> created;
    const bool discard = target.verb == DragDrop::DropVerb::Discard;
    const FileOps::Result result =
        discard ? FileOps::Delete(window, sources, /*permanently=*/false, &error)
                : (move ? FileOps::Move(window, sources, target.path, &created, &error)
                        : FileOps::Copy(window, sources, target.path, &created, &error));

    // Un annullamento non e' un guasto, ma non e' nemmeno "fatto": un gesto che
    // finisce senza dire niente e' indistinguibile da un difetto, e chi ha appena
    // mollato un file deve poter sapere che non e' successo niente.
    if (result == FileOps::Result::Cancelled)
    {
        app->dropReported = true;
        ReportStatus(*app, discard ? L"Eliminazione annullata" : L"Operazione annullata");
        return;
    }

    if (result == FileOps::Result::Failed)
    {
        DiagLog("  caduta non riuscita: " + ToUtf8(error));
        app->dropReported = true;
        ReportStatus(*app, (discard ? L"Eliminazione non riuscita: "
                                    : (move ? L"Spostamento non riuscito: "
                                            : L"Copia non riuscita: ")) +
                               error);
        return;
    }

    // La vista si rifa' sempre: uno spostamento toglie gli originali da qui, e una
    // copia puo' aver aggiunto qualcosa se la destinazione e' la cartella aperta.
    // Si seleziona solo cio' che e' arrivato *qui*, perche' quello che e' finito
    // altrove non si vede e non si potrebbe evidenziare.
    std::unordered_set<std::wstring> keep;
    std::wstring landed;
    if (!discard && _wcsicmp(target.path.c_str(), app->directory.c_str()) == 0)
    {
        for (const std::wstring& path : created)
        {
            const std::wstring name = LeafName(path);
            keep.insert(name);
            if (landed.empty())
                landed = name;
        }
    }
    ReloadKeeping(*app, keep, landed, scroll);

    app->dropReported = true;
    DiagLog("  esito della caduta: " + std::string(discard ? "eliminazione"
                                                           : (move ? "spostamento" : "copia")) +
            " di " + std::to_string(sources.size()) + " elementi" +
            (discard ? std::string()
                     : ", prodotti " + std::to_string(created.size())));
    ReportStatus(*app, (discard ? L"Buttati via " : (move ? L"Spostati " : L"Copiati ")) +
                           std::to_wstring(sources.size()) +
                           (discard ? L" elementi nel cestino"
                                    : L" elementi in " + LeafName(target.path)));
}

} // namespace DragDrop

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    EnableHighDpi();
    // `OleInitialize` e non `CoInitializeEx`: il trascinamento fra finestre lo
    // richiede, e fa anche quello che faceva l'altra. L'appartamento deve essere
    // quello singolo, ed e' quello che usavamo gia'.
    OleInitialize(nullptr);

    // Optional starting directory, the same convenience `explorer` offers:
    //   Bolt.exe D:\some\folder
    std::wstring startDirectory;
    {
        int argumentCount = 0;
        if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount))
        {
            if (argumentCount > 1)
            {
                const DWORD attributes = GetFileAttributesW(arguments[1]);
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    startDirectory = arguments[1];
                // Un nome di spazio dei nomi non ha attributi da leggere: si
                // riconosce da come e' scritto, ed e' l'unico modo di aprire Bolt
                // direttamente sul cestino.
                else if (ShellLocation::IsNamespace(arguments[1]))
                    startDirectory = arguments[1];
            }
            LocalFree(arguments);
        }
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    windowClass.lpfnWndProc   = WindowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    // L'icona arriva dalle risorse dell'eseguibile, cosi' la barra delle
    // applicazioni, Alt+Tab ed Esplora risorse mostrano la stessa immagine.
    // LR_SHARED: l'icona e' di proprieta' del sistema e non va distrutta.
    windowClass.hIcon         = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BOLT),
                                                              IMAGE_ICON, 32, 32, LR_SHARED));
    windowClass.hIconSm       = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BOLT),
                                                              IMAGE_ICON, 16, 16, LR_SHARED));
    windowClass.lpszClassName = L"BoltWindow";
    if (RegisterClassExW(&windowClass) == 0)
    {
        ShowError("RegisterClassExW failed.");
        return 1;
    }

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Bolt",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1240, 760,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
    {
        ShowError("CreateWindowExW failed.");
        return 1;
    }

    // L'icona registrata con la classe il sistema la usa solo come ripiego: una
    // finestra dichiara le proprie, ed e' quella dichiarata che legge la barra
    // delle applicazioni. Senza questo WM_GETICON risponde 0.
    SendMessageW(window, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(windowClass.hIcon));
    SendMessageW(window, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(windowClass.hIconSm));

    // Togliere l'area non-cliente toglie anche quello che DWM disegnava attorno:
    // ombra e angoli arrotondati, cioe' quello che rende una finestra
    // riconoscibile come finestra su Windows 11. Senza, il bordo e' un salto
    // netto di colore e l'applicazione sembra piatta accanto alle altre.
    //
    // Estendere il riquadro di DWM di un pixel per lato glieli restituisce: DWM
    // disegna il riquadro DENTRO l'area cliente, quindi la didascalia non torna.
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(window, &margins);

    // Il riquadro di DWM segue il tema di sistema, che puo' essere chiaro mentre
    // l'applicazione e' scura: una linea bianca su un bordo scuro si vede.
    const BOOL darkFrame = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame,
                          sizeof(darkFrame));

    // Gli angoli arrotondati di Windows 11 valgono per le finestre con
    // didascalia; questa non ne ha, quindi vanno chiesti.
    const DWORD cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference,
                          sizeof(cornerPreference));

    HDC   dc      = nullptr;
    HGLRC context = nullptr;
    if (!CreateOpenGLContext(window, &dc, &context))
    {
        ShowError("Could not create an OpenGL context.");
        return 1;
    }

    const char* missingFunction = nullptr;
    if (!LoadOpenGLFunctions(&missingFunction))
    {
        ShowError(std::string("Missing OpenGL entry point: ") + (missingFunction ? missingFunction : "?"));
        return 1;
    }

    AppState app;
    std::string error;
    if (!app.renderer.Initialize(REA_SHADER_DIR, &error))
    {
        ShowError(error);
        return 1;
    }

    // One atlas size for every layer, so the glyph, icon and thumbnail UVs
    // cannot diverge. Layer 0 holds the body glyphs, layer 1 the shell icons,
    // layer 2 the file thumbnails, layer 3 the secondary glyphs.
    // Il livello delle texture serve anche a `ApplyFonts`, quindi la costante sta
    // in cima al file; qui restano le due misure dell'anteprima.
    constexpr int kPreviewCellSize  = 128;
    constexpr int kPreviewCapacity  = 64;

    // Sei strati: i quattro di sempre, il carattere a spaziatura fissa e l'immagine
    // dell'anteprima grande.
    if (!app.renderer.CreateTextureArray(/*layers=*/6, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.font.Build(app.renderer, L"Segoe UI", 15, kGlyphLayer, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    // The secondary size is rasterised rather than scaled down. Drawing the body
    // atlas at 0.8-0.95 resamples glyphs that were hinted for 15 px, which is
    // exactly where small text loses its crispness; a second body costs one more
    // atlas layer and makes the whole secondary tier sharp.
    if (!app.smallFont.Build(app.renderer, L"Segoe UI", 13, kSmallGlyphLayer,
                             kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    // Il carattere del testo. Se non c'e' — non tutte le installazioni hanno
    // Consolas — si ripiega su quello secondario: si legge lo stesso, le colonne non
    // sono allineate, e va detto invece di far finta.
    app.monoFontOk = app.monoFont.Build(app.renderer, L"Consolas", 14, kMonoLayer,
                                        kTextureLayerSize, &error);
    if (!app.monoFontOk)
        error.clear();

    if (!app.icons.Initialize(app.renderer, kIconLayer, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.previews.Initialize(app.renderer, kPreviewLayer, kTextureLayerSize,
                                 kPreviewCellSize, kPreviewCapacity, &error))
    {
        ShowError(error);
        return 1;
    }

    // Folder statistics run on their own worker pool, sized by the volume
    // profile of whatever is being counted.
    app.stats.Start();

    RECT client{};
    GetClientRect(window, &client);
    app.viewportWidth  = client.right;
    app.viewportHeight = client.bottom;

    // Scale the UI to the monitor DPI, then let the user zoom from there.
    const UINT dpi = GetDpiForWindow(window);
    app.dpiScale = (dpi > 0) ? static_cast<float>(dpi) / 96.0f : 1.0f;
    app.zoom = app.dpiScale;

    // Le preferenze si rileggono **prima** della prima costruzione della vista: la
    // lingua e l'aspetto devono essere quelli giusti al primo fotogramma.
    LoadSettings(app);
    // L'atlante dei glifi e' stato costruito con i valori di partenza; se il file
    // delle impostazioni ne chiede altri, si rifa' adesso. Sono le stesse tre
    // misure che comanda il pannello delle opzioni, e si validano la' dentro.
    if (app.fontFace != L"Segoe UI" || app.uiFontSize != 15 ||
        app.monoFace != L"Consolas" || app.monoFontSize != 14)
        ApplyFonts(app);
    InitColumns(app);
    BuildPlaces(app);
    app.window = window;

    const std::wstring initialDirectory = startDirectory.empty()
        ? (app.places.empty() ? L"C:\\" : app.places[0].path)
        : startDirectory;

    app.tabs.push_back(Tab{});
    app.activeTab = 0;
    LoadDirectory(app, initialDirectory);

    app.deviceContext = dc;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    // Da qui in poi la finestra accetta file trascinati da fuori. Se il sistema
    // non vuole registrarla lo si dice: un trascinamento che non arriva sembra un
    // difetto dell'applicazione, ed e' un limite del sistema.
    if (!DragDrop::Register(window))
        ReportStatus(app, L"Trascinamento in entrata non disponibile su questo sistema.");

    ShowWindow(window, SW_SHOW);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    app.lastFrameTime = NowSeconds();
    bool running = true;

    while (running)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running)
            break;

        const double now = NowSeconds();
        const double delta = now - app.lastFrameTime;
        app.lastFrameTime = now;
        if (delta > 0.0)
            app.framesPerSecond = static_cast<float>(0.9 * app.framesPerSecond + 0.1 * (1.0 / delta));

        if (app.viewportWidth <= 0 || app.viewportHeight <= 0)
            continue;

        // The view is rebuilt only when something that affects it changes: the
        // filter, the grouping, the sort, the folder. A grouping that depends on
        // the statistics is refreshed while they arrive, but throttled, so one
        // publish per work slice does not become one rebuild per frame.
        if (GroupMenu* menu = OpenMenu(app))
        {
            if (menu->dirty)
                GroupMenuRebuild(*menu);
        }

        const uint64_t statsRevision = app.stats.Revision();
        if (app.viewDirty ||
            (app.groupMode != GroupMode::None && statsRevision != app.viewStatsRevision &&
             now - app.lastViewRebuild > 0.25))
        {
            RebuildView(app);
            app.viewStatsRevision = statsRevision;
            app.lastViewRebuild   = now;
        }

        // `HoverToSelect`: fermandosi su una riga la si seleziona, dopo una pausa
        // breve. Non mentre si trascina e non con un menu aperto: sono gesti che
        // stanno usando il puntatore per un'altra cosa.
        if (app.hoverToSelect && app.hovered >= 0 && !app.dragArmed &&
            OpenMenu(app) == nullptr && !app.contextMenu.open &&
            now - app.hoverStart > 0.35 && !app.renameActive)
        {
            const Entry& entry = app.entries[static_cast<size_t>(app.hovered)];
            if (app.selection.count(entry.wideName) == 0 || app.cursor != entry.wideName)
            {
                app.selection.clear();
                app.selection.insert(entry.wideName);
                app.cursor = entry.wideName;
            }
        }

        RenderFrame(app);
    }

    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    app.stats.Stop();
    OleUninitialize();
    app.previews.Shutdown();
    app.renderer.Shutdown();
    wglMakeCurrent(nullptr, nullptr);
    if (context != nullptr)
        wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    CoUninitialize();
    return 0;
}
