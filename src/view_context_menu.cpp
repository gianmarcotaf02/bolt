// ---------------------------------------------------------------------------
// Il menu contestuale: la superficie — dove sta, come si legge, cosa si clicca.
//
// I verbi non si leggono qui (`shell_menu.*` li legge), e nemmeno si eseguono: qui
// c'e' la **forma** del menu — i livelli, la geometria delle righe, lo scorrimento,
// il disegno, il passaggio del puntatore, il clic, i tasti — e le azioni che il menu
// sa fare: aprire un sottomenu, eseguire una voce, chiedere le proprieta'.
//
// Uscito da `main.cpp` il 2026-10-18, dopo i verbi della shell: e' la superficie che
// sblocca l'input del mouse, perche' il corpo di `WM_LBUTTONDOWN` **e'** l'azione
// della superficie che riceve l'evento. Il menu resta un **albero di livelli** — un
// livello aperto per volta sopra il precedente — perche' e' quello che serve a
// disegnare e a leggere il puntatore; chi lo riempie decide cosa c'e' dentro.
// ---------------------------------------------------------------------------
#include "view_context_menu.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
// `std::tolower`: la ricerca non distingue le maiuscole, e la regola e' quella di
// `MenuMatch` (vedi `ContextLabelMatch`).
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "app_actions.h"
#include "commands.h"
#include "app_ui.h"
#include "diag.h"
#include "drag_drop.h"
#include "file_ops.h"
#include "home.h"
#include "language.h"
#include "navigation.h"
#include "shell_location.h"
#include "shell_menu.h"
#include "shell_new.h"
#include "shell_sendto.h"
#include "view_inspector.h"
#include "view_list.h"
#include "view_statusbar.h"

namespace Bolt
{
namespace
{
// Le voci di "Nuovo file" hanno una banda loro, per lo stesso motivo degli id della
// shell: l'indice e' la posizione del modello nell'elenco letto dal registro.
constexpr int kNewItemCmdFirst = 0x7000;
constexpr int kNewItemCmdLast  = 0x70FF;

// Le destinazioni di "Invia a": stessa banda per lo stesso motivo, l'indice e' la
// posizione nell'elenco letto dalla cartella SendTo.
constexpr int kSendToCmdFirst = 0x7100;
constexpr int kSendToCmdLast  = 0x71FF;

// L'altezza della riga del campo di ricerca. E' una costante e non una misura del
// testo perche' la leggono la geometria — l'altezza del pannello, dove comincia
// l'elenco, lo spazio che resta alle voci — e il disegno: due misure della stessa
// cosa sono due bersagli sbagliati.
constexpr float kContextSearchHeight = 34.0f;

// Fa combaciare un termine di ricerca con l'etichetta di una voce: tutti i termini
// devono comparire, in qualunque ordine, senza distinzione di maiuscole.
//
// E' la **stessa regola** di `MenuMatch` in `view_menu.cpp`, e la scelta e'
// deliberata: chi cerca "nuo car" nella palette e "nuo car" nel menu contestuale
// deve ottenere la stessa cosa, perche' e' la stessa ricerca fatta in due posti. Due
// componenti (vedi `ContextMenu` in `app_state.h`) che condividono una regola e non
// un tipo si portano dietro questo: la regola vive in due punti, e chi ne cambia una
// deve cambiare anche l'altra.
bool ContextLabelMatch(const std::string& text, const std::wstring& filter)
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

// Vero se la voce si vede nell'elenco che si sta mostrando.
//
// Il filtro vale per il livello 0 e basta: un sottomenu parla di quello che sta
// **dentro** la voce scelta, e filtrarlo cambierebbe il contenuto di quella voce
// invece di cercare nel menu che si ha davanti.
bool LevelItemVisible(const ContextMenu& menu, int depth, const ContextMenuItem& item)
{
    if (depth != 0 || menu.filter.empty())
        return true;
    // Un separatore non ha un'etichetta da confrontare, e sotto filtro resterebbe
    // in mezzo a due voci che non ci sono piu': una riga vuota che non separa
    // niente.
    if (item.separator)
        return false;
    return ContextLabelMatch(item.label, menu.filter);
}

// Quante voci del livello si vedono. E' la misura su cui si contano la capacita'
// del pannello, la barra di scorrimento e la posizione della scelta.
int LevelVisibleCount(const ContextMenu& menu, int depth)
{
    if (depth != 0 || menu.filter.empty())
        return LevelCount(menu, depth);

    const std::vector<ContextMenuItem>* items = LevelItems(menu, depth);
    if (items == nullptr)
        return 0;
    int count = 0;
    for (const ContextMenuItem& item : *items)
        if (LevelItemVisible(menu, depth, item))
            ++count;
    return count;
}

// L'indice **nella lista delle voci** della voce che si vede in posizione
// |position|, o -1 se non c'e'.
//
// Il disegno, il puntatore e il cursore ragionano per posizioni visibili; le voci
// e `openPath` per indici. Con il filtro vuoto le due numerazioni coincidono, ed
// e' per questo che il cambio si e' potuto fare senza toccare la sostanza: questa
// funzione e' l'unico punto in cui le due si incontrano.
int LevelVisibleIndex(const ContextMenu& menu, int depth, int position)
{
    if (position < 0)
        return -1;
    if (depth != 0 || menu.filter.empty())
        return position < LevelCount(menu, depth) ? position : -1;

    const std::vector<ContextMenuItem>* items = LevelItems(menu, depth);
    if (items == nullptr)
        return -1;
    int seen = 0;
    for (int i = 0; i < static_cast<int>(items->size()); ++i)
    {
        if (!LevelItemVisible(menu, depth, (*items)[static_cast<size_t>(i)]))
            continue;
        if (seen == position)
            return i;
        ++seen;
    }
    return -1;
}

const ContextMenuItem* LevelVisibleItem(const ContextMenu& menu, int depth, int position)
{
    return LevelItem(menu, depth, LevelVisibleIndex(menu, depth, position));
}

// La voce ha qualcosa dentro? Vale anche per un sottomenu della shell **non
// ancora letto**: il suo contenuto non esiste prima di aprirlo, e chiederlo per
// tutti a ogni tasto destro costerebbe quanto interrogare la shell dieci volte.
bool HasSubmenu(const ContextMenuItem& item)
{
    return item.shellSubmenu != nullptr || !item.children.empty() || item.lazyNewFile ||
           item.lazySendTo;
}

// Se una voce del livello porta un'icona, tutte le etichette si spostano di una
// colonna. La decisione e' **per livello** e non per voce: un menu con le icone su
// otto righe e il testo a due rientri diversi si legge peggio di un menu senza
// icone, perche' l'occhio segue la colonna e la trova spezzata.
bool LevelHasIcons(const AppState& app, int depth)
{
    const int total = LevelCount(app.contextMenu, depth);
    for (int i = 0; i < total; ++i)
    {
        const ContextMenuItem* item = LevelItem(app.contextMenu, depth, i);
        if (item != nullptr && (item->shellBitmap != nullptr || !item->iconSpec.empty()))
            return true;
    }
    return false;
}

// Lo spazio di una colonna d'icona: icona e distanza dal testo. Derivato dalla
// misura del corpo, non scelto a occhio, cosi' cresce con l'ingrandimento.
float ContextMenuIconColumn(const Layout& layout)
{
    return 20.0f * layout.scale;
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
        // L'etichetta piu' la sua scorciatoia, con lo spazio che le separa: il pannello
        // deve entrarci, o la scorciatoia finisce sopra il nome.
        float rowWidth = app.font.Measure(item->label, scale);
        if (!item->shortcut.empty())
            rowWidth += 28.0f * scale + app.smallFont.Measure(item->shortcut, scale);
        widest = (std::max)(widest, rowWidth);
        if (HasSubmenu(*item))
            arrow = 18.0f * scale;   // lo spazio del segno di sottomenu
    }

    if (LevelHasIcons(app, depth))
        widest += ContextMenuIconColumn(layout);

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
    // e' lo spazio delle voci. Il sottotitolo ce l'ha solo il primo livello, e la
    // riga del campo di ricerca pure: sono spazio che le voci non hanno.
    const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
    const float search = (depth == 0) ? kContextSearchHeight * scale : 0.0f;
    const float room = static_cast<float>(app.viewportHeight) - 8.0f * scale -
                       12.0f * scale - footer - search;

    // Contando le voci che si **vedono**: sotto filtro sono meno di quelle che ci
    // sono, e il pannello si stringe su quelle.
    const int total = LevelVisibleCount(app.contextMenu, depth);
    float used  = 0.0f;
    int   count = 0;
    for (int i = firstItem; i < total; ++i)
    {
        const ContextMenuItem* item = LevelVisibleItem(app.contextMenu, depth, i);
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
        const ContextMenuItem* item = LevelVisibleItem(menu, depth, i);
        content += (item != nullptr && item->separator) ? separator : layout.rowHeight;
    }

    const float width = ContextMenuWidth(app, layout, depth);
    // 22 px in fondo per il sottotitolo, che c'e' solo nel primo livello.
    const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
    // La riga del campo di ricerca e' **dentro** il pannello ed e' una riga in piu':
    // se non entrasse nell'altezza, l'ultima voce cadrebbe fuori dal fondo.
    const float search = (depth == 0) ? kContextSearchHeight * scale : 0.0f;
    const float height = content + padding * 2.0f + footer + search;

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
    return (std::min)(LevelVisibleCount(app.contextMenu, depth), first + visible);
}

Rect ContextMenuRowRect(const AppState& app, const Layout& layout, int depth, int index)
{
    const Rect panel       = ContextMenuPanel(app, layout, depth);
    const float scale      = layout.scale;
    const float separator  = 7.0f * scale;
    const float padding    = 6.0f * scale;

    const ContextMenu& menu = app.contextMenu;
    // Il campo di ricerca sta **dentro** il pannello, sopra la prima voce: se
    // l'elenco non cominciasse dopo di lui, il disegno e il puntatore leggerebbero
    // due righe diverse — la prima sarebbe il campo per uno dei due.
    float top = panel.y + padding + (depth == 0 ? kContextSearchHeight * scale : 0.0f);
    for (int i = menu.levels[static_cast<size_t>(depth)].firstItem; i < index; ++i)
    {
        const ContextMenuItem* item = LevelVisibleItem(menu, depth, i);
        top += (item != nullptr && item->separator) ? separator : layout.rowHeight;
    }

    const ContextMenuItem* item = LevelVisibleItem(menu, depth, index);
    const float height = (item != nullptr && item->separator) ? separator : layout.rowHeight;
    return Rect{panel.x + 4.0f * scale, top, panel.w - 8.0f * scale, height};
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
        ReportStatus(app, L"\"" + targets[index].label +
                              TrW(L"\" non ha accettato i file.",
                                  L"\" did not accept the files."));
        return;
    }

    DiagLog("  invio a voce " + std::to_string(index) + ": riuscito");

    // "Avviato", non "fatto": la destinazione puo' copiare, comprimere o aprire
    // una finestra sua, e il suo esito non e' nostro da dichiarare.
    ReportStatus(app, TrW(L"Avviato: ", L"Launched: ") + targets[index].label);
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

// Apre il sottomenu della voce che si vede in posizione |position| del livello
// |depth|. Restituisce falso se quella voce un sottomenu non ce l'ha, o se e'
// rimasto vuoto.
//
// |position| e' la posizione fra le voci **che si vedono**, quella che il cursore
// tiene e che il clic ha sotto, mentre `openPath` indicizza le voci: le due
// numerazioni coincidono finche' il filtro e' vuoto, e il passaggio da una
// all'altra si fa qui, una volta sola.
bool OpenSubmenu(AppState& app, const Layout& layout, int depth, int position)
{
    ContextMenu& menu = app.contextMenu;
    const int index = LevelVisibleIndex(menu, depth, position);
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
    const int count = LevelVisibleCount(menu, newDepth);
    for (int i = 0; i < count; ++i)
    {
        const ContextMenuItem* child = LevelVisibleItem(menu, newDepth, i);
        if (child != nullptr && !child->separator && !child->disabled)
        {
            level.selected = i;
            break;
        }
    }
    ContextMenuClampWindow(app, layout, newDepth);
    return true;
}

// Disegna un livello del menu. Il livello 0 e' il menu, gli altri i sottomenu:
// stesso pannello, senza sottotitolo, e col segno che dice quali voci ne hanno
// uno dentro.
void ContextMenuDrawLevel(AppState& app, const Layout& layout, int depth)
{
    ContextMenu& menu = app.contextMenu;
    // `Animations`: il menu compare in dissolvenza. Il sottomenu no: si apre mentre
    // il primo e' gia' li', e un secondo fade dentro il primo si vedrebbe come uno
    // sfarfallio.
    app.renderer->SetOpacity(depth == 0 ? PanelAppear(menu.openedAt, app.animations) : 1.0f);

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = ContextMenuPanel(app, layout, depth);
    const ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];

    r.Draw(UIDraw::Shadow(Rect{panel.x + 2.0f * scale, panel.y + 5.0f * scale, panel.w, panel.h},
                          t.shadow, 20.0f * scale, 8.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 8.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 8.0f * scale));

    // Il campo di ricerca, in cima e solo sul livello 0. Il suo spazio si riserva
    // **anche col filtro vuoto**: un pannello che cambia altezza alla prima lettera
    // digitata sposta il menu sotto il puntatore, e il bersaglio successivo non e'
    // piu' dove lo si e' lasciato.
    if (depth == 0)
    {
        const float fieldTop = panel.y + 6.0f * scale;
        const std::string query = menu.filter.empty() ? menu.placeholder : ToUtf8(menu.filter);
        // Centrato nella riga, non a un `y` scelto a occhio: la misura la da' il
        // carattere, e con l'ingrandimento cambia con lui.
        const float textY = fieldTop +
                            (kContextSearchHeight * scale - app.font.LineHeight() * scale) * 0.5f;
        app.font.Draw(r, panel.x + 14.0f * scale, textY, query,
                      menu.filter.empty() ? t.textDim : t.text, scale);
        if (!menu.filter.empty())
        {
            const float caretX =
                panel.x + 14.0f * scale + app.font.Measure(query, scale) + 1.0f * scale;
            r.FillRect(Rect{caretX, textY, 1.0f, app.font.LineHeight() * scale}, t.text);
        }
        // Lo stacco fra il campo e la prima voce, un pixel sopra l'inizio
        // dell'elenco: sulla riga esatta lo coprirebbe il fondo della voce scelta,
        // e sparirebbe proprio quando la si guarda.
        const float ruleY = fieldTop + kContextSearchHeight * scale - 1.0f;
        r.FillRect(Rect{panel.x + 1.0f, ruleY, panel.w - 2.0f, 1.0f}, t.separator);
    }

    // Solo le voci dentro la finestra del menu: quelle fuori non si disegnano, e
    // quello che non si disegna non sborda fuori dal pannello.
    for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
    {
        const ContextMenuItem* item = LevelVisibleItem(menu, depth, i);
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

        // L'icona, quando la voce ne ha una. Non tutte le voci ne hanno: quelle del
        // menu moderno di Windows 11 non consegnano la bitmap a chi legge il menu
        // classico, e il registro non ne dichiara una. Si disegna solo quello che
        // c'e': un'icona indovinata sarebbe peggio di nessuna icona.
        const float iconColumn = ContextMenuIconColumn(layout);
        const bool  icons      = LevelHasIcons(app, depth);
        if (icons)
        {
            const ShellIconAtlas::IconUV* uv = nullptr;
            if (item->shellBitmap != nullptr)
                uv = app.icons.GetForMenuBitmap(item->iconKey, item->shellBitmap);
            else if (!item->iconSpec.empty())
                uv = app.icons.GetForIconSpec(item->iconSpec);

            if (uv != nullptr)
            {
                const float side = 16.0f * scale;
                r.Draw(UIDraw::Image(Rect{row.x + 8.0f * scale,
                                               row.y + (row.h - side) * 0.5f, side, side},
                                     Color{1.0f, 1.0f, 1.0f, 1.0f},
                                     static_cast<float>(kIconLayer),
                                     uv->u0, uv->v0, uv->u1, uv->v1));
            }
        }

        const float labelX = row.x + 10.0f * scale + (icons ? iconColumn : 0.0f);
        app.font.Draw(r, labelX, TextY(layout, row, app.font), item->label,
                      item->disabled ? t.textDim : t.text, scale);

        // La scorciatoia, a destra e nel carattere dei dettagli: e' la stessa cosa che
        // fanno i menu di Windows, e serve a imparare i tasti senza aprire le opzioni.
        float rightReserved = 14.0f * scale;
        if (!item->shortcut.empty())
        {
            const float width = app.smallFont.Measure(item->shortcut, scale);
            app.smallFont.Draw(r, row.right() - 12.0f * scale - width,
                               TextY(layout, row, app.smallFont), item->shortcut,
                               item->disabled ? t.textDim.WithAlpha(0.55f) : t.textDim, scale);
            rightReserved += width + 12.0f * scale;
        }

        // Il segno che la voce ne contiene altre. L'atlante dei glifi e' ASCII,
        // quindi e' un '>' e non una punta: si legge uguale, e non chiede un
        // carattere che l'atlante non ha.
        if (HasSubmenu(*item))
            app.font.Draw(r, row.right() - rightReserved - 6.0f * scale,
                          TextY(layout, row, app.font), ">", t.textDim, scale);
    }

    // La barra dice che sopra o sotto c'e' dell'altro: un menu che scorre senza
    // dirlo sembra un menu a cui mancano delle voci.
    const int total   = LevelVisibleCount(menu, depth);
    const int visible = ContextMenuLastVisible(app, layout, depth) - level.firstItem;
    if (visible < total)
    {
        const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
        // La barra sta accanto alle voci, non accanto al campo di ricerca: la sua
        // altezza e' quella dell'elenco, o direbbe che si puo' scorrere il campo.
        const float header = (depth == 0) ? kContextSearchHeight * scale : 0.0f;
        const float trackY = panel.y + 6.0f * scale + header;
        const float trackH = panel.h - 12.0f * scale - footer - header;
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
} // namespace

int ActiveDepth(const ContextMenu& menu)
{
    return static_cast<int>(menu.openPath.size());
}

// Tiene la finestra del menu dentro i limiti e ci tiene dentro la voce scelta:
// l'Invio esegue quello che si vede, quindi la selezione non deve poter uscire
// dal pannello.
void ContextMenuClampWindow(AppState& app, const Layout& layout, int depth,
                            bool followSelection)
{
    ContextMenu& menu = app.contextMenu;
    if (depth < 0 || depth >= static_cast<int>(menu.levels.size()))
        return;

    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    // Le voci che si vedono, non quelle che ci sono: sotto filtro sono le uniche
    // che il pannello puo' mostrare, e la capacita' va misurata su quelle.
    const int total = LevelVisibleCount(menu, depth);
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
    // Il filtro muore con il menu: la prossima apertura parte dall'elenco intero,
    // non da quello che si stava cercando l'altra volta.
    menu.filter.clear();
    menu.shellIsBackground = false;
    // Il bersaglio **non** si cancella qui: `ContextMenuActivate` chiude il menu e
    // poi esegue il comando, quindi il comando lo legge dopo la chiusura. Lo
    // sostituisce il menu successivo, o la palette (che agisce sulla cartella
    // aperta e lo azzera).
    ShellMenuDetach(menu);   // gli oggetti restano in memoria, per la stessa selezione
}

void BuildContextMenu(AppState& app, float x, float y)
{
    ContextMenu& menu = app.contextMenu;
    const DWORD handlesStart = OpenHandleCount();
    menu.items.clear();
    // Il segnaposto e il filtro vuoto: questo menu si apre mostrando tutto, e il
    // campo dice cosa ci si puo' scrivere dentro.
    menu.placeholder = Tr("Cerca un comando...", "Find a command...");
    menu.filter.clear();
    menu.x = x;
    menu.y = y;
    menu.openPath.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.shellIsBackground = false;
    // Il menu della lista parla della cartella aperta: le voci che agiscono su un
    // percorso (il fissaggio) prendono questa, non quella della barra laterale.
    menu.openedAt = NowSeconds();
    app.contextTargetPath  = app.directory;
    app.contextTargetLabel = ToUtf8(LeafName(app.directory));
    ClosePinMenu(app);

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
    // La Home si comporta come una cartella di spazio dei nomi per il menu: le
    // voci che agiscono su un percorso si spengono. Le sue righe nominano cartelle
    // vere, e un `Elimina` li' cancellerebbe una cartella che non si sta guardando.
    const bool home = IsHome(app.directory);
    const bool noFileOps = namespaced || home;

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
        menu.items.push_back({Tr("Taglia", "Cut"), CmdCut, false, noFileOps});
        menu.items.push_back({Tr("Copia", "Copy"), CmdCopy, false, noFileOps});

        // "Invia a" qui e non in fondo: e' dove sta in Esplora risorse, subito
        // dopo il blocco Taglia/Copia, e li' la mano lo cerca.
        DiagLog("  handle: prima di 'Invia a' +" +
                std::to_string(OpenHandleCount() - handlesStart));
        if (!ShellSendTo::Targets().empty())
        {
            ContextMenuItem sendTo;
            sendTo.label      = Tr("Invia a", "Send to");
            sendTo.lazySendTo = true;
            sendTo.disabled   = noFileOps;
            menu.items.push_back(std::move(sendTo));
        }
        menu.items.push_back({"", 0, true});
        menu.items.push_back({Tr("Rinomina", "Rename"), CmdRename, false, noFileOps});
        menu.items.push_back({Tr("Elimina", "Delete"), CmdDelete, false, noFileOps});
        menu.items.push_back({Tr("Elimina definitivamente", "Delete permanently"),
                              CmdDeletePermanent, false, noFileOps});
        menu.items.push_back({"", 0, true});
        menu.items.push_back({Tr("Proprietà", "Properties"), kContextProperties, false, noFileOps});
        menu.items.push_back({Tr("Proprietà di Windows", "Windows properties"),
                              kContextPropertiesWindows, false, noFileOps});

        // I verbi della shell stanno dopo i nostri: i nostri sono quelli che si
        // usano davvero, e non devono scendere sotto una lista lunga quanto le
        // estensioni installate su questa macchina.
        //
        // Sulla Home non si chiedono: la cartella aperta non e' un percorso, e i
        // verbi della shell su `bolt:home` non esistono. Le righe sono cartelle
        // vere, ma i loro verbi sono le stesse operazioni appena spente.
        menu.items.push_back({"", 0, true});
        if (!home)
            AppendShellItems(app, menu);
    }
    else
    {
        // Col tasto destro sul fondo, invece, la shell apre il menu: quello e' il
        // contesto di cio' su cui si e' cliccato.
        if (!home)
            AppendShellItems(app, menu);
    }

    menu.items.push_back({count > 0 ? Tr("Incolla qui", "Paste here") : Tr("Incolla", "Paste"),
                          CmdPaste, false, noFileOps});
    menu.items.push_back({Tr("Nuova cartella", "New folder"), CmdNewFolder, false, noFileOps});

    // "Nuovo file": i tipi che Windows sa creare, dal registro. Se il registro non
    // ne ha nessuno la voce non si mette, perche' un sottomenu vuoto non si spiega.
    DiagLog("  handle: dopo 'Invia a' e i verbi +" +
            std::to_string(OpenHandleCount() - handlesStart));
    if (!ShellNew::Templates().empty())
    {
        ContextMenuItem newFile;
        newFile.label       = Tr("Nuovo file", "New file");
        newFile.lazyNewFile = true;
        newFile.disabled    = noFileOps;
        menu.items.push_back(std::move(newFile));
    }

    menu.items.push_back({"", 0, true});
    menu.items.push_back({Tr("Aggiorna", "Refresh"), CmdRefresh});
    menu.items.push_back({Tr("Apri un terminale qui", "Open a console here"), CmdOpenConsole, false,
                          noFileOps});

    // Il fissaggio, e solo sul **fondo**: agisce sulla cartella aperta, non sulla
    // selezione, e li' sotto non c'e' una selezione con cui confonderlo. Con delle
    // voci scelte, un "fissa in..." vorrebbe dire "fissa la voce scelta" — che non e'
    // quello che questo comando fa.
    if (count == 0 && !home)
    {
        menu.items.push_back({"", 0, true});
        menu.items.push_back({Tr("Fissa in...", "Pin to..."), CmdPinTo});
        menu.items.push_back({Tr("Togli da...", "Unpin from..."), CmdUnpin});
        // Il layout della cartella: sta sul fondo perche' parla della cartella, non
        // di una voce. `DirHostLayoutSave`/`DirHostLayoutDel` nel riferimento.
        menu.items.push_back({"", 0, /*separator=*/true});
        menu.items.push_back({Tr("Salva il layout di questa cartella",
                                 "Save this folder's layout"), CmdSaveFolderLayout});
        menu.items.push_back({Tr("Dimentica il layout di questa cartella",
                                 "Forget this folder's layout"), CmdForgetFolderLayout});
    }

    DiagLog("  handle: dopo 'Nuovo file' +" +
            std::to_string(OpenHandleCount() - handlesStart));
    menu.open = true;
}

// Il menu di una **riga della barra laterale**. Non e' il menu della lista con
// altre voci: e' un altro contesto, e le voci sono quelle che hanno senso su una
// destinazione — fissarla, toglierla da dove sta, toglierla dai recenti.
//
// Il percorso non si indovina: lo prende dal bersaglio sotto il puntatore e lo
// lascia in `app.contextTargetPath`, dove lo trovano i comandi.
void BuildSidebarContextMenu(AppState& app, float x, float y, const ClickTarget& row)
{
    ContextMenu& menu = app.contextMenu;
    menu.items.clear();
    menu.x = x;
    menu.y = y;
    menu.openPath.clear();
    // Anche questo menu ha il campo: e' un menu contestuale come l'altro, e chi ha
    // imparato a cercare una voce su una riga lo cerca anche su una destinazione.
    menu.placeholder = Tr("Cerca un comando...", "Find a command...");
    menu.filter.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.shellIsBackground = false;
    menu.shell   = nullptr;      // nessun verbo della shell: non e' la lista
    menu.shellMenu = nullptr;

    menu.openedAt = NowSeconds();
    app.contextTargetPath  = row.path;
    app.contextTargetLabel = row.label.empty() ? ToUtf8(LeafName(row.path)) : row.label;
    menu.subtitle = app.contextTargetLabel;

    ClosePinMenu(app);

    // Le due voci del fissaggio: agiscono sul percorso della riga, quindi non
    // serve aprirla prima. `BuildPinMenu` dice da solo quando non c'e' niente da
    // fare, e in quel caso l'esito finisce nella barra di stato.
    menu.items.push_back({Tr("Fissa in...", "Pin to..."), CmdPinTo});
    menu.items.push_back({Tr("Togli da...", "Unpin from..."), CmdUnpin});
    menu.items.push_back({"", 0, /*separator=*/true});
    menu.items.push_back({Tr("Rimuovi dai recenti", "Remove from recents"), CmdForgetRecent});
    menu.open = true;
}

void ContextMenuMove(AppState& app, const Layout& layout, int delta)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int count = LevelVisibleCount(menu, depth);
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
        const ContextMenuItem* item = LevelVisibleItem(menu, depth, index);
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
    const int count = LevelVisibleCount(menu, depth);
    if (count == 0)
        return;

    const int step = toFirst ? 1 : -1;
    int index = toFirst ? -1 : count;
    for (int i = 0; i < count; ++i)
    {
        index = (index + step + count * 2) % count;
        const ContextMenuItem* item = LevelVisibleItem(menu, depth, index);
        if (item != nullptr && !item->separator && !item->disabled)
            break;
    }
    level.selected = index;
    ContextMenuClampWindow(app, layout, depth);
}

// La digitazione e il `Backspace` nel menu aperto vanno al campo di ricerca del
// livello 0. Con un sottomenu aperto no: il filtro cambierebbe le righe del
// livello sotto quello che si sta guardando, e il sottomenu sta appeso a una di
// quelle — un elenco che si riordina sotto il pannello che si sta leggendo e'
// peggio di un tasto che non fa niente.
static void ContextMenuAfterFilter(AppState& app)
{
    ContextMenu& menu = app.contextMenu;
    // Le posizioni di adesso non sono quelle di prima: una scelta ferma su un
    // numero vecchio indicherebbe un'altra voce, o nessuna, e l'Invio eseguirebbe
    // quello che capita. Si riparte dalla prima voce eseguibile, che e' anche
    // quella che ci si aspetta di avere sotto l'Invio dopo aver scritto.
    menu.levels[0].selected  = -1;
    menu.levels[0].firstItem = 0;
    ContextMenuJump(app, ComputeLayout(app), true);
}

void ContextMenuChar(AppState& app, wchar_t character)
{
    ContextMenu& menu = app.contextMenu;
    if (!menu.open || ActiveDepth(menu) != 0)
        return;
    if (character < 0x20 || character == 0x7F)
        return;
    menu.filter.push_back(character);
    ContextMenuAfterFilter(app);
}

void ContextMenuBackspace(AppState& app)
{
    ContextMenu& menu = app.contextMenu;
    if (!menu.open || ActiveDepth(menu) != 0 || menu.filter.empty())
        return;
    menu.filter.pop_back();
    ContextMenuAfterFilter(app);
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
    const int selected = menu.levels[static_cast<size_t>(depth)].selected;
    const ContextMenuItem* chosen = LevelVisibleItem(menu, depth, selected);
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
            ReportStatus(app, TrW(L"Comando della shell non piu' disponibile.",
                                  L"Shell command no longer available."));
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
            ReportStatus(app, TrW(L"Il comando della shell non e' partito.",
                                  L"The shell command did not start."));
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
        // In-app: le stesse righe dell'inspector, in una finestra nostra.
        app.contextMenu.open = false;
        OpenPropertiesPanel(app);
        return;
    }
    if (item.command == kContextPropertiesWindows)
    {
        // Un'azione che non fa niente in silenzio e' peggio di una che non parte:
        // se non si apre niente, l'utente deve sapere perche'.
        if (paths.empty())
            ReportStatus(app, TrW(L"Niente su cui aprire le proprieta'.",
                                  L"Nothing to show properties for."));
        else if (ShowProperties(app.window, paths) == 0)
            ReportStatus(app, TrW(L"Proprietà non disponibili per la selezione.",
                                  L"Properties unavailable for the selection."));
        return;
    }

    // Tutto il resto e' un comando che esiste gia': il menu contestuale non
    // aggiunge capacita', rende raggiungibile dove serve quella che c'e'.
    RunCommand(app, item.command);
}

void ContextMenuDraw(AppState& app, const Layout& layout)
{
    if (!app.contextMenu.open)
        return;

    // Dal primo livello in giu': ogni pannello disegna sopra il precedente, che e'
    // l'ordine in cui stanno sullo schermo.
    for (int depth = 0; depth <= ActiveDepth(app.contextMenu); ++depth)
        ContextMenuDrawLevel(app, layout, depth);

    app.renderer->SetOpacity(1.0f);   // l'animazione e' di questo menu, non del resto
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
        // `i` e' una posizione fra le voci che **si vedono**: sotto filtro le due
        // numerazioni non coincidono, e il puntatore deve cadere sulla riga che ha
        // sotto, non sull'indice che quella riga aveva prima di filtrare.
        for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
        {
            const ContextMenuItem* item = LevelVisibleItem(menu, depth, i);
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
} // namespace Bolt
