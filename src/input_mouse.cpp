// ---------------------------------------------------------------------------
// Il mouse: la rotella, il movimento, i tre pulsanti, il cursore.
//
// I `case` sono usciti da `WindowProc` il 2026-10-23, con il testo intatto e lo
// stesso rientro (`docs/05` §2, passo 2). Con loro sono venute le due funzioni che
// solo il mouse chiama: `SingleClickActivates` — la regola del clic singolo, che il
// riferimento espone come opzione — e `GroupMenuClick`, rimasta in `main.cpp`
// perche' chiedeva a `HitTest` dove sono le righe; adesso `HitTest` e' un modulo
// come gli altri, quindi la ragione e' caduta.
// ---------------------------------------------------------------------------
#include "input.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app_actions.h"
#include "app_ui.h"
#include "commands.h"
#include "drag_drop.h"
#include "file_ops.h"
#include "home.h"
#include "input_targets.h"
#include "language.h"
#include "navigation.h"
#include "shell_location.h"
#include "settings.h"
#include "view_statusbar.h"
#include "ui_renderer.h"
#include "view_context_menu.h"
#include "view_list.h"
#include "view_menu.h"
#include "view_options.h"
#include "view_update.h"

namespace Bolt
{
namespace
{
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
        // Una voce spenta non fa niente, e non lo fa **in silenzio**: il clic cade su
        // una riga che si vede spenta, e il menu resta aperto. Chiuderlo e' la
        // risposta giusta a una scelta, e qui non c'e' stata nessuna scelta.
        //
        // E il cursore **non si sposta**: posarlo su una voce morta la farebbe
        // sembrare scelta, e chi preme `Invio` subito dopo non otterrebbe niente da
        // una riga che si vede accesa.
        if (GroupMenuItemEnabled(menu, row->index))
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
        return MenuAction::None;
    }

    // Inside the panel but not on a row: swallow the click. Outside it, close.
    return MenuPanel(app, layout, menu).Contains(x, y) ? MenuAction::None : MenuAction::Close;
}
} // namespace

LRESULT HandleMouseMessage(HWND window, AppState* app, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
        case WM_MOUSEWHEEL:
            if (app != nullptr)
            {
                // La finestra dell'aggiornamento e' modale: la rotella non scorre
                // niente, nemmeno se il puntatore e' fuori dal suo rettangolo.
                if (UpdateDialogVisible(*app))
                    return 0;

                const Layout layout = ComputeLayout(*app);

                // Il pannello dell'operazione sta sopra a tutto ma non e' piu' un
                // muro: la rotella **sopra di lui** non scorre l'elenco che gli sta
                // dietro (sembrerebbe che non risponda), fuori da lui la finestra
                // funziona come sempre.
                if (OperationPanelVisible(*app))
                {
                    POINT wheel{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                    ScreenToClient(window, &wheel);
                    if (OperationPanelRect(*app, layout)
                            .Contains(static_cast<float>(wheel.x), static_cast<float>(wheel.y)))
                        return 0;
                }

                // Il pannello delle proprieta' e' modale: la rotella lo scorre e
                // basta.
                if (app->propertiesOpen)
                {
                    const int steps = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                    app->propertiesScroll -= static_cast<float>(steps) * 44.0f;
                    if (app->propertiesScroll < 0.0f)
                        app->propertiesScroll = 0.0f;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }

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
                // In un menu il cursore **e'** la scelta, quindi e' giusto cosi'.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    // Il pannello delle opzioni e' l'eccezione, e non e' un
                    // dettaglio: li' le voci si leggono, e muovere il cursore
                    // accenderebbe una riga dopo l'altra portandosi via la
                    // selezione a chi stava solo scorrendo. Scorre l'elenco, di
                    // tre righe per tacca, e il cursore resta dov'e'. Il delta e'
                    // frazionario apposta: la rotella di un touchpad manda valori
                    // piccoli, e troncati a un intero non scorrono affatto.
                    if (menu->richRows)
                    {
                        const float rowHeight = MenuRowHeight(layout, *menu);
                        const Rect  panel     = MenuPanel(*app, layout, *menu);
                        const float room      = MenuListBottom(layout, panel) -
                                                MenuListTop(layout, panel, *menu);
                        const float wheelSteps =
                            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                        GroupMenuScrollBy(*menu, -wheelSteps * 3.0f * rowHeight, rowHeight, room);
                        InvalidateRect(window, nullptr, FALSE);
                        return 0;
                    }

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
                app->hovered        = -1;
                app->hoveredPlace   = -1;
                app->hoveredTab     = -1;
                app->hoveredTabPane = -1;
                app->hoveredChrome  = -1;
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

            // Sull'**intestazione** il tasto destro apre le operazioni della colonna
            // sotto il puntatore — adattala, chiudila, riporta quelle di partenza. Il
            // filtro per valori resta sull'imbuto: sono due gesti, e prima erano lo
            // stesso — il tasto destro faceva il mestiere dell'imbuto.
            {
                const Layout layout = ComputeLayout(*app);
                if (layout.header.Contains(x, y))
                {
                    const int column = ColumnAtPoint(*app, layout, x);
                    if (column >= 0)
                    {
                        BuildColumnOpsMenu(*app, column);
                        return 0;
                    }
                }
            }

            // Sulla **barra laterale** il menu e' quello della riga: le voci che
            // agiscono su un percorso — fissare, togliere, togliere dai recenti —
            // prendono quella riga, non la cartella aperta. E' il gesto che prima
            // mancava: per fissare un recente bisognava aprirlo.
            if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
            {
                BuildSidebarContextMenu(*app, x, y, *place);
                return 0;
            }

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
                    app->tabPress < static_cast<int>(
                                        app->panes[static_cast<size_t>(app->activePane)].tabs.size()))
                {
                    app->dragTab =
                        app->panes[static_cast<size_t>(app->activePane)]
                            .tabs[static_cast<size_t>(app->tabPress)]
                            .id;
                    app->tabPress = -1;
                }
                if (app->dragTab >= 0)
                {
                    UpdateTabDrop(*app, tx, ty);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }

            // Il **divisore** dei pannelli: mentre lo si trascina la sua posizione e' il
            // puntatore, con i limiti del modello — un pannello piu' stretto di un quarto
            // non mostra nemmeno i nomi. Il valore resta al rilascio e vive quanto la
            // sessione, come la larghezza dell'ispettore: e' una forma della finestra,
            // non una preferenza da scrivere in un file.
            if (app != nullptr && app->splitDrag >= 0)
            {
                if ((wparam & MK_LBUTTON) == 0)
                {
                    app->splitDrag = -1;
                    ReleaseCapture();
                    return 0;
                }
                const Layout frame = WindowLayout(*app);
                const float scale = frame.scale;
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                const float gap = kSplitterWidth * scale;
                float fraction = app->splitFraction;
                if (app->splitColumn)
                {
                    const float usable = (std::max)(2.0f, frame.header.w - gap);
                    fraction = (x - frame.header.x) / usable;
                }
                else
                {
                    // Dalla cima della fascia delle schede, che e' dove comincia il
                    // primo pannello: con l'intestazione il divisore si fermerebbe
                    // trentaquattro pixel sotto il punto in cui lo si e' preso.
                    const float top = (frame.tabs.h > 0.0f) ? frame.tabs.y : frame.header.y;
                    const float usable = (std::max)(2.0f, frame.filterBar.bottom() - top - gap);
                    fraction = (y - top) / usable;
                }
                app->splitFraction = std::clamp(fraction, 0.25f, 0.75f);
                // `viewDirty` non serve: le righe non dipendono dalla larghezza del
                // pannello, e la griglia si accorge da se' che la misura e' cambiata.
                InvalidateRect(window, nullptr, FALSE);
                return 0;
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
            // Il bordo sinistro dell'ispettore si prende e si trascina come il bordo di
            // una colonna: la finestra non si sposta, il pannello si allarga.
            if (app != nullptr && app->inspectorDrag >= 0)
            {
                if ((wparam & MK_LBUTTON) == 0)
                {
                    app->inspectorDrag = -1;
                    RememberSettings(*app);
                }
                else
                {
                    const float x = static_cast<float>(GET_X_LPARAM(lparam));
                    const float scale = app->zoom;
                    // Il pannello non si stringe sotto i 200 px e non si prende piu' di
                    // meta' finestra: sotto, i valori non si leggono; sopra, la lista
                    // sparisce.
                    const float sidebar = app->sidebarVisible ? 186.0f * scale : 0.0f;
                    const float least   = 200.0f;
                    const float most    = (static_cast<float>(app->viewportWidth) - sidebar - 260.0f * scale) / scale;
                    app->inspectorWidth = std::clamp(app->inspectorDragStartWidth -
                                                         (x - app->inspectorDragStartX) / scale,
                                                     least, (std::max)(least, most));
                    app->viewDirty = true;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }

            if (app != nullptr && app->columnDrag > 0)
            {
                const Layout layout = ComputeLayout(*app);
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const int index = app->columnDrag;
                const float moved = app->columnDragStartFraction +
                                    (x - app->columnDragStartX) / (std::max)(1.0f, layout.header.w);
                const float minimum = kMinColumnWidth * layout.scale /
                                      (std::max)(1.0f, layout.header.w);
                // I limiti sono i bordi **visibili**: con una colonna spenta in mezzo,
                // i suoi due bordi coincidono e il trascinamento deve poter arrivare
                // fino alla colonna prima, o non potrebbe riprendersi lo spazio.
                float edges[kColumnCount + 1] = {};
                ColumnEdges(*app, edges);
                const int prev = PrevVisibleColumn(*app, index);
                const int next = NextVisibleColumn(*app, index);
                const float left  = (prev >= 0 ? edges[prev] : 0.0f) + minimum;
                const float right = (next < kColumnCount ? edges[next] : 1.0f) - minimum;
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
                    // La selezione si vede **mentre** si trascina, che e' l'unico modo di
                    // sapere cosa si sta per prendere prima di lasciare il tasto.
                    ApplyRectSelection(*app);
                    InvalidateRect(window, nullptr, FALSE);
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
                                ReportStatus(*app, TrW(L"La shell non sa preparare queste voci per il "
                                                       L"trascinamento: si gestiscono col menu "
                                                       L"contestuale.",
                                                       L"The shell cannot prepare these items for "
                                                       L"dragging: use the context menu."));
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
                            // Il messaggio lo da' chi ha eseguito la caduta, se e'
                            // stato uno di noi: sa dove sono finiti i file, e
                            // "Spostati 3 elementi" senza dire dove e' meno di
                            // "Buttati via 3 elementi nel cestino". Qui si parla
                            // solo se la destinazione era un'altra applicazione,
                            // che non ha modo di dirlo.
                            //
                            // Se l'ha eseguita questa finestra l'operazione sta
                            // ancora girando e la vista si rifa' quando finisce:
                            // rifarla adesso la svuoterebbe per poi rimetterla, che
                            // e' un lampo e non un'informazione.
                            if (!app->dropReported)
                            {
                                ReloadKeeping(*app, /*keep=*/{}, /*preferCursor=*/{}, app->scroll);
                                ReportStatus(*app, VerbItems(dragged, L"Spostato ", L"Spostati ",
                                                             L"Moved "));
                            }
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

                // Il pannello **sotto il puntatore** prende il fuoco, come ogni finestra
                // che si divide: si guarda quello che si sta per toccare, e i tasti vanno
                // li'. Solo quando non c'e' un gesto in corso — durante un rettangolo o
                // un trascinamento il pannello non deve cambiare sotto le mani — e mai
                // con un menu aperto, che copre quello che c'e' sotto.
                if (app->panes.size() > 1 && OpenMenu(*app) == nullptr && !app->rectSelecting &&
                    app->dragTab < 0 && !app->dragArmed)
                {
                    for (int pane = 0; pane < static_cast<int>(app->panes.size()); ++pane)
                    {
                        if (pane == app->activePane)
                            continue;
                        const Layout other = PaneLayout(*app, pane);
                        // Anche la **fila delle schede** e' parte del pannello: passandoci
                        // sopra si sta gia' guardando quello, e le schede devono
                        // rispondere al sovrappasso come il resto del suo riquadro.
                        if (other.tabs.Contains(x, y) || other.list.Contains(x, y) ||
                            other.header.Contains(x, y) || other.filterBar.Contains(x, y))
                        {
                            ActivatePane(*app, pane);
                            break;
                        }
                    }
                }
                app->hovered = -1;
                app->hoveredPlace = -1;
                app->hoveredTab = -1;
                app->hoveredTabPane = -1;
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
                else if (HitTest(*app, x, y, ClickTarget::Kind::SidebarToggle))  app->hoveredChrome = 5;

                const int wasHovered = app->hovered;
                // L'indice da solo non basta piu': la scheda 1 c'e' in tutte e due le file,
                // e il pannello a cui appartiene e' quello che il bersaglio porta con se'.
                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                {
                    app->hoveredTab     = tab->index;
                    app->hoveredTabPane = tab->pane;
                }
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

                // La finestra dell'aggiornamento e' modale: finche' e' aperta i clic
                // sono suoi, e la lista sotto non deve nemmeno accorgersene.
                if (UpdateDialogVisible(*app))
                {
                    UpdateDialogClick(*app, x, y);
                    return 0;
                }
                // Il pannello **sotto il puntatore** diventa quello attivo, e il gesto
                // prosegue li'. Le coordinate non si traducono: sono le stesse, e cambia
                // il layout — che da qui in poi e' quello del pannello appena attivato,
                // perche' `ComputeLayout` e' il suo. E' il motivo per cui ogni hit test
                // sotto questa riga continua a funzionare senza saperne niente.
                if (app->panes.size() > 1)
                {
                    for (int pane = 0; pane < static_cast<int>(app->panes.size()); ++pane)
                    {
                        if (pane == app->activePane)
                            continue;
                        const Layout other = PaneLayout(*app, pane);
                        // La fila delle schede fa parte del pannello: un clic su una sua
                        // scheda e' un clic su di lui, e il pannello deve prendere il
                        // fuoco **prima** che il clic venga eseguito — e' l'unico modo
                        // perche' una scheda del pannello di sotto risponda al primo
                        // colpo invece che al secondo.
                        if (other.tabs.Contains(x, y) || other.list.Contains(x, y) ||
                            other.header.Contains(x, y) || other.filterBar.Contains(x, y))
                        {
                            ActivatePane(*app, pane);
                            break;
                        }
                    }
                }


                // Il pannello su cui si e' cliccato prende il fuoco: e' l'unica cosa
                // che `InactivePanelContentOpacity` deve sapere, e la si aggiorna qui
                // perche' qui si sa **dove** si e' cliccato.
                {
                    const Layout probe = ComputeLayout(*app);
                    if (probe.sidebar.Contains(x, y))
                        app->focusArea = AppState::FocusArea::Sidebar;
                    else if (probe.inspector.Contains(x, y))
                        app->focusArea = AppState::FocusArea::Inspector;
                    else if (probe.list.Contains(x, y))
                        app->focusArea = AppState::FocusArea::List;
                }

                // Il pannello dell'operazione non e' piu' modale: sopra di lui un clic
                // o preme `Annulla`, o non fa niente (il puntatore e' sopra un
                // pannello, e quello che c'e' sotto non deve ricevere il clic); fuori
                // da lui la finestra si usa come sempre — navigare, selezionare,
                // copiare — e le operazioni che ne nascono vanno in coda.
                if (OperationPanelVisible(*app))
                {
                    const Layout layout = ComputeLayout(*app);
                    const Rect panel = OperationPanelRect(*app, layout);
                    if (panel.Contains(x, y))
                    {
                        if (OperationCancelButton(*app, layout, panel).Contains(x, y))
                            CancelOperation(*app);
                        return 0;
                    }
                }

                // Il pannello delle proprieta' e' modale: il clic o cade sul suo
                // pulsante, o lo chiude, o non fa altro.
                if (app->propertiesOpen)
                {
                    const Layout layout = ComputeLayout(*app);
                    const Rect panel = PropertiesPanelRect(*app, layout);
                    if (PropertiesWindowsButton(*app, layout, panel).Contains(x, y))
                    {
                        app->propertiesOpen = false;
                        const std::vector<std::wstring> paths = SelectedPaths(*app);
                        if (paths.empty() || ShowProperties(app->window, paths) == 0)
                            ReportStatus(*app,
                                         TrW(L"Proprietà non disponibili per la selezione.",
                                             L"Properties unavailable for the selection."));
                    }
                    else if (!panel.Contains(x, y))
                    {
                        app->propertiesOpen = false;
                    }
                    return 0;
                }

                // Un bordo d'intestazione si prende e si trascina. La presa viene
                // prima di tutto il resto: mentre si trascina, la lista sotto non
                // deve rispondere ai clic.
                // Il bordo sinistro dell'ispettore si prende da una striscia di sei pixel:
                // un bordo di un pixel non si prende col mouse, e il pannello resterebbe
                // largo quello che e' per sempre.
                {
                    const Layout layout = ComputeLayout(*app);
                    const Rect grip{layout.inspector.x - 3.0f * layout.scale, layout.inspector.y,
                                    6.0f * layout.scale, layout.inspector.h};
                    if (grip.Contains(x, y))
                    {
                        app->inspectorDrag           = 0;
                        app->inspectorDragStartX     = x;
                        app->inspectorDragStartWidth = app->inspectorWidth;
                        SetCapture(window);
                        return 0;
                    }
                }

                // Il **divisore** fra i due pannelli, con la stessa idea: sei pixel di
                // presa, e mentre lo si trascina si prende tutto (la cattura fa arrivare
                // i movimenti anche fuori dalla finestra).
                {
                    const Rect splitter = SplitterRect(*app);
                    if (splitter.Contains(x, y))
                    {
                        app->splitDrag = 0;
                        SetCapture(window);
                        return 0;
                    }
                }

                if (const ClickTarget* edge = HitTest(*app, x, y, ClickTarget::Kind::ColumnEdge))
                {
                    app->columnDrag = edge->index;
                    app->columnDragStartX = x;
                    app->columnDragStartFraction = app->columns[edge->index].fraction;
                    // Da qui in poi la larghezza e' quella scelta a mano: "adatta" era il
                    // valore di partenza, e chi trascina la sta cambiando.
                    app->autoFitColumns = false;
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
                    // Le due pastiglie della palette si prendono il clic prima della
                    // riga: sono sopra di lei, e `HitTest` legge al contrario.
                    if (menu == &app->palette)
                    {
                        if (const ClickTarget* hit =
                                HitTest(*app, x, y, ClickTarget::Kind::PaletteStar))
                        {
                            const int command =
                                app->palette.items[static_cast<size_t>(hit->index)].id;
                            if (app->favoriteCommands.count(command) != 0)
                                app->favoriteCommands.erase(command);
                            else
                                app->favoriteCommands.insert(command);
                            RememberSettings(*app);
                            BuildPaletteMenu(*app);
                            return 0;
                        }
                        if (const ClickTarget* hit =
                                HitTest(*app, x, y, ClickTarget::Kind::PaletteAssign))
                        {
                            // Assegnare un tasto **da qui**: si apre la mappa gia'
                            // posata su quel comando e la cattura del tasto comincia.
                            const int command =
                                app->palette.items[static_cast<size_t>(hit->index)].id;
                            app->palette.open = false;
                            BuildKeyMapMenu(*app, command);
                            BeginKeyCapture(*app, command);
                            return 0;
                        }
                    }

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
                        // E chiudere l'editor dei tasti non deve lasciare appesa una
                        // cattura: un clic fuori dal pannello mentre si aspetta il
                        // tasto nuovo e' un modo normale di ripensarci.
                        if (menu == &app->keyMap)
                            CloseKeyMap(*app);
                        else
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
                        else if (menu == &app->keyMap)
                            RunKeyMapSelection(*app);
                        else if (menu == &app->pinMenu)
                            RunPinMenuSelection(*app);
                        else if (menu == &app->columnOps)
                            RunColumnOpsSelection(*app);
                        else
                            RunColumnMenuSelection(*app);
                    }
                    return 0;
                }

                // La linguetta del filtro: apre l'elenco dei valori di quella
                // colonna, o richiude quello che c'e' gia'.
                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::ColumnFilter))
                {
                    // Il nome e' la prima colonna, e il suo filtro non e' un elenco di
                    // valori: la linguetta accende la casella di testo in fondo, che
                    // e' il filtro del nome che serve davvero.
                    if (tab->index == 0)
                    {
                        app->columnMenu.open = false;
                        app->columnPanel     = -1;
                        app->filterActive    = true;
                    }
                    else if (app->columnPanel == tab->index && app->columnMenu.open)
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

                // I selettori di vista e lo stepper delle anteprime, nella barra in
                // fondo. Cambiare vista da qui e' l'unico gesto col puntatore: prima
                // c'era solo la palette.
                // Il campo della ricerca: il clic lo accende. Un secondo clic dentro lo
                // lascia com'e' — la tastiera e' gia' sua.
                if (HitTest(*app, x, y, ClickTarget::Kind::SearchField))
                {
                    app->searchFieldActive = true;
                    app->filterActive      = false;
                    return 0;
                }

                if (const ClickTarget* v = HitTest(*app, x, y, ClickTarget::Kind::ViewSelect))
                {
                    app->viewMode  = static_cast<ViewMode>(v->index);
                    app->viewDirty = true;
                    return 0;
                }
                if (HitTest(*app, x, y, ClickTarget::Kind::NewFolder))
                {
                    RunCommand(*app, CmdNewFolder);
                    return 0;
                }
                if (HitTest(*app, x, y, ClickTarget::Kind::PreviewDown))
                {
                    if (app->previewStep > 0)
                        --app->previewStep;
                    RememberSettings(*app);
                    return 0;
                }
                if (HitTest(*app, x, y, ClickTarget::Kind::PreviewUp))
                {
                    if (app->previewStep + 1 < AppState::kPreviewStepCount)
                        ++app->previewStep;
                    RememberSettings(*app);
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
                    // La croce e' della scheda **di quel pannello**: se non e' quello
                    // attivo si porta li' il fuoco, perche' chiudere una scheda in un
                    // pannello che non si sta guardando deve lasciare lo sguardo dove
                    // sta. `ActivatePane` non salva niente di sbagliato: i campi vivi
                    // sono di un pannello e passano nel suo tab.
                    if (closeTab->pane >= 0 && closeTab->pane != app->activePane)
                        ActivatePane(*app, closeTab->pane);
                    CloseTab(*app, closeTab->index);
                }
                else if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                {
                    // Il pannello della scheda prima della scheda: e' l'unico modo
                    // perche' cliccare un tab del pannello di sotto funzioni al primo
                    // colpo, invece di doverlo prima attivare e poi cliccare.
                    if (tab->pane >= 0 && tab->pane != app->activePane)
                        ActivatePane(*app, tab->pane);
                    ActivateTab(*app, tab->index);
                    // Il gesto puo' diventare un trascinamento, ma solo se il mouse
                    // si muove: un clic non deve spostare niente.
                    app->tabPress  = tab->index;
                    app->tabPressX = x;
                }
                else if (const ClickTarget* newTab = HitTest(*app, x, y, ClickTarget::Kind::TabNew))
                {
                    // Anche il `+` e' di un pannello: una scheda nuova si apre **li'**.
                    if (newTab->pane >= 0 && newTab->pane != app->activePane)
                        ActivatePane(*app, newTab->pane);
                    NewTab(*app, app->directory.empty() ? L"C:\\" : app->directory);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::Menu))
                {
                    const Layout layout = ComputeLayout(*app);
                    const Rect button{layout.titleBar.x + 6.0f * layout.scale,
                                      layout.titleBar.y + 7.0f * layout.scale,
                                      30.0f * layout.scale, layout.titleBar.h - 14.0f * layout.scale};
                    BuildAppMenu(*app, button.x, button.bottom() + 2.0f * layout.scale);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::SidebarToggle))
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
                    // La Home ha tre bande e non si raggruppa: il controllo lo
                    // dice, invece di ciclare un valore che la lista non legge.
                    if (IsHome(app->directory))
                    {
                        ReportStatus(*app, TrW(L"La Home raggruppa per banda",
                                               L"Home groups by band"));
                    }
                    else
                    {
                        app->groupMode = static_cast<GroupMode>((static_cast<int>(app->groupMode) + 1) % 6);
                        app->viewDirty = true;
                    }
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
                        // La base su cui il rettangolo lavora: quello che l'utente ha
                        // gia' scelto con `Ctrl`, o niente quando il rettangolo
                        // sostituisce la selezione.
                        app->rectBase = app->selection;
                    }
                }
            }
            return 0;

        case WM_LBUTTONUP:
            // Il divisore: al rilascio il valore resta dov'e' — non e' una preferenza, e
            // non si scrive da nessuna parte.
            if (app != nullptr && app->splitDrag >= 0)
            {
                app->splitDrag = -1;
                ReleaseCapture();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            // Lasciando il bordo dell'ispettore si chiude il trascinamento **e** si
            // ricorda la larghezza: aspettare il primo movimento del mouse dopo il
            // rilascio lasciava il pannello largo e la preferenza vecchia.
            if (app != nullptr && app->inspectorDrag >= 0)
            {
                app->inspectorDrag = -1;
                ReleaseCapture();
                RememberSettings(*app);
                return 0;
            }
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
                if (HitTest(*app, x, y, ClickTarget::Kind::ColumnEdge) != nullptr)
                {
                    // Doppio clic su un bordo: torna **ad adattare**, che e' il valore di
                    // partenza. Prima riportava la colonna alla frazione scritta nella
                    // tabella, che era lo stesso gesto ma con un numero indovinato a mano.
                    app->autoFitColumns = true;
                    FitColumns(*app);
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
                // Il **divisore** dei pannelli: una freccia a due punte, nel verso in cui
                // si sposta. Il verso lo dice l'asse, non la posizione del puntatore —
                // cosi' il cursore non cambia mentre lo si trascina.
                if (app->panes.size() > 1 &&
                    SplitterRect(*app).Contains(static_cast<float>(point.x),
                                                static_cast<float>(point.y)))
                {
                    SetCursor(LoadCursorW(nullptr, app->splitColumn ? IDC_SIZEWE : IDC_SIZENS));
                    return TRUE;
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;

    }

    return kMessageNotHandled;
}
} // namespace Bolt
