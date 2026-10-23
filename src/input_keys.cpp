// ---------------------------------------------------------------------------
// I tasti: `WM_KEYDOWN`, `WM_CHAR`, `WM_SYSKEYDOWN`, `WM_SYSCHAR`.
//
// I quattro `case` sono usciti da `WindowProc` il 2026-10-23, quando le superfici
// erano gia' fuori e i corpi non avevano piu' niente da trascinarsi dietro
// (`docs/05` §2, passo 2 della decisione sul confine). **Il testo dei casi non e'
// stato toccato**: e' lo stesso, con lo stesso rientro, dentro uno `switch` che
// vive qui. Un `break` che nel `WindowProc` usciva dallo `switch` e arrivava a
// `DefWindowProcW` adesso esce da questo `switch` e arriva a `kMessageNotHandled`,
// che e' la stessa cosa detta al chiamante.
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
#include "language.h"
#include "navigation.h"
#include "ui_renderer.h"
#include "home.h"
#include "settings.h"
#include "view_statusbar.h"
#include "view_context_menu.h"
#include "view_list.h"
#include "view_menu.h"
#include "view_options.h"
#include "view_update.h"

namespace Bolt
{
// La finestra e `lparam` non servono a nessun tasto: la firma resta quella del mouse
// perche' chi legge la chiamata in `WindowProc` deve vedere due funzioni uguali, e i
// due parametri che oggi non si usano restano senza nome invece di essere scartati.
LRESULT HandleKeyMessage(HWND, AppState* app, UINT message, WPARAM wparam, LPARAM)
{
    switch (message)
    {
        case WM_KEYDOWN:
            // La finestra dell'aggiornamento e' modale e prende **tutti** i tasti:
            // `Esc` annulla o chiude, `Invio` preme il pulsante, e il resto non deve
            // arrivare a un comando che sta sotto.
            if (app != nullptr && UpdateDialogVisible(*app))
            {
                UpdateDialogKey(*app, wparam);
                return 0;
            }
            if (app != nullptr)
            {
                // Il pannello dell'operazione non prende piu' la tastiera: prende
                // `Esc`, che e' l'annullamento, e nient'altro. Il resto va dove
                // andrebbe sempre — e se un comando avvia un'operazione, quella va in
                // coda.
                if (OperationPanelVisible(*app) && wparam == VK_ESCAPE)
                {
                    CancelOperation(*app);
                    return 0;
                }

                // Il pannello delle proprieta' e' modale come un menu: prende la
                // tastiera prima di tutto il resto, e non lascia passare niente.
                if (app->propertiesOpen)
                {
                    PropertiesPanelKey(*app, wparam);
                    return 0;
                }

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

                    // La cattura del tasto sta **prima** di ogni altra lettura: mentre
                    // e' aperta il tasto premuto e' il dato che si sta scrivendo, non
                    // un comando — altrimenti `Ctrl+P` aprirebbe il GoTo invece di
                    // diventare il tasto di un comando.
                    if (app->keyCaptureCommand >= 0)
                    {
                        KeyMapKeyDown(*app, wparam);
                        return 0;
                    }

                    // Ogni menu si richiude col suo tasto. Le scorciatoie sono
                    // quelle del riferimento: Ctrl+P e F4 al GoTo, Ctrl+Maiusc+P ai
                    // comandi, Ctrl+, alle opzioni.
                    const bool toggling = control &&
                        ((menu == &app->goTo    && wparam == 'P' && !shift) ||
                         (menu == &app->palette && wparam == 'P' && shift)  ||
                         ((menu == &app->options || menu == &app->keyMap) &&
                          wparam == VK_OEM_COMMA));
                    if (toggling)
                    {
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        // Dall'editor si **torna alle opzioni**, perche' e' da li' che si
                        // e' aperto: il tasto che le apre le riapre, invece di
                        // chiudere tutto e lasciare il posto dov'era.
                        const bool backToOptions = (menu == &app->keyMap);
                        menu->open = false;
                        if (backToOptions)
                            BuildOptionsMenu(*app);
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
                        // Il campo di ricerca si cancella da qui e non da `WM_CHAR`,
                        // che il `Backspace` non lo produce: il carattere che ne
                        // uscirebbe e' un controllo (0x08), e un controllo non e'
                        // testo da cercare.
                        case VK_BACK:   ContextMenuBackspace(*app);        break;
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
                        // (il campo della ricerca ha il suo `Backspace` piu' sotto)
                        case VK_BACK:
                            // Con una selezione, `Backspace` la cancella tutta: e' il
                            // gesto che ci si aspetta da un campo di testo, e qui la
                            // selezione e' l'unica cosa che si puo' cancellare in blocco.
                            if (app->renameSelectEnd > 0)
                            {
                                app->renameText.erase(0, app->renameSelectEnd);
                                app->renameSelectEnd = 0;
                                app->renameCaret     = 0;
                            }
                            else if (app->renameCaret > 0)
                            {
                                // Indietro di **un carattere**, non di un byte: un
                                // nome con un accento occupa due byte, e cancellarne
                                // uno solo lascerebbe un mezzo carattere nel nome.
                                size_t from = app->renameCaret - 1;
                                while (from > 0 &&
                                       (static_cast<unsigned char>(app->renameText[from]) & 0xC0) == 0x80)
                                    --from;
                                app->renameText.erase(from, app->renameCaret - from);
                                app->renameCaret = from;
                            }
                            break;
                        default:
                            break;
                    }
                    return 0;
                }

                // Prima di tutto il resto: **che comando e' questo tasto**? La
                // risposta la da' la mappa (un dato, che si puo' cambiare); i tasti
                // che non sono comandi — le frecce, l'Invio, `Ctrl+A` che seleziona
                // tutto, `Ctrl+rotella` — restano qui sotto, perche' sono
                // comportamenti dell'input e non voci di un menu.
                {
                    const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    const int command = app->keys.CommandFor(static_cast<unsigned>(wparam), ctrl, shift, alt);
                    if (command >= 0)
                    {
                        // **Lo stesso esecutore della palette e dei menu.** Prima il
                        // tasto e il comando erano due strade con lo stesso corpo
                        // scritto due volte — `case 'C': DoCopyToClipboard(...)` qui e
                        // `case CmdCopy:` la' — e due copie divergono sempre.
                        RunCommand(*app, command);
                        return 0;
                    }
                }

                switch (wparam)
                {
                    case VK_ESCAPE:
                        // Escape unwinds the filter and stops there. Closing the
                        // window is the ✕ button or Alt+F4: an application that
                        // exits on a bare Escape is a trap, and this one did.
                        //
                        // Prima del filtro c'e' l'aggiornamento: se e' in corso, Esc
                        // e' l'unica cosa che lo puo' fermare.
                        if (app->updateJob != nullptr && app->updateJob->Running())
                        {
                            app->updateJob->Cancel();
                            ReportStatus(*app, TrW(L"Annullamento...", L"Cancelling..."));
                            break;
                        }
                        // Il campo della ricerca, prima del filtro: `Esc` ferma la
                        // ricerca e riporta la cartella, che e' quello che ci si aspetta
                        // da un campo in cui si e' appena scritto.
                        if (app->search.active || app->search.finished ||
                            app->searchFieldActive || !app->searchText.empty())
                        {
                            StopSearch(*app);
                            break;
                        }
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
                    case VK_RETURN:
                        // Nel campo della ricerca `Invio` fa partire la ricerca; ovunque
                        // altrove apre la voce scelta.
                        if (app->searchFieldActive && !app->searchText.empty())
                        {
                            StartSearch(*app, app->searchText);
                            app->searchFieldActive = false;
                            break;
                        }
                        ActivateSelection(*app);
                        break;
                    case VK_BACK:
                        // Il campo della ricerca si cancella come un campo di testo, e
                        // non fa salire di livello: chi sta scrivendo non sta navigando.
                        if (app->searchFieldActive)
                        {
                            if (!app->searchText.empty())
                                app->searchText.pop_back();
                            break;
                        }
                        // Backspace edits the filter while it is active, and only
                        // navigates to the parent when there is nothing to erase.
                        if (app->filterActive && !app->filter.empty())
                        {
                            app->filter.pop_back();
                            app->viewDirty = true;
                        }
                        else
                        {
                            // Sale di un livello, e da una radice di volume va a
                            // "This PC": la stessa strada del comando, non una
                            // seconda che puo' divergere.
                            RunCommand(*app, CmdGoUp);
                        }
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
                    case 'G':
                        // GoTo ha preso Ctrl+P e F4, come il riferimento. Ctrl+G
                        // resta libero: ciclare il raggruppamento sta su
                        // Ctrl+Maiusc+G, che e' l'unico uso rimasto.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                            (GetKeyState(VK_SHIFT) & 0x8000) != 0)
                        {
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
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case WM_CHAR:
            // Come per le proprieta': il carattere lo ha gia' preso `WM_KEYDOWN`, e qui
            // non deve finire nel filtro della lista sotto la finestra.
            if (app != nullptr && UpdateDialogVisible(*app))
                return 0;
            if (app != nullptr)
            {
                // La cattura di un tasto vuole **il tasto**, non il carattere che ne
                // esce: senza questo, premendo `C` per rimappare Copia il `c`
                // finirebbe nel filtro dell'elenco sotto.
                if (app->keyCaptureCommand >= 0)
                    return 0;

                // Il pannello delle proprieta' e' modale, e i suoi tasti li ha gia'
                // presi `WM_KEYDOWN`: senza questo, un carattere digitato andrebbe
                // nel filtro della lista **sotto** il pannello.
                if (app->propertiesOpen)
                    return 0;

                // CharacterKeyAction defaults to typing into the filter rather
                // than triggering a command, so a printable character over the
                // list starts filtering. Backspace is handled in WM_KEYDOWN: not
                // handling it here avoids erasing two characters per press.
                const wchar_t character = static_cast<wchar_t>(wparam);
                if (character >= 0x20 && character != 0x7F)
                {
                    if (app->renameActive)
                    {
                        // Il primo carattere **sostituisce** la selezione (il nome
                        // senza estensione): e' quello che rende `F2` + scrivere una
                        // rinomina e non un'aggiunta. I caratteri non validi in un nome
                        // di file li rifiuta `IFileOperation` con un messaggio: meglio
                        // non anticiparli, cosi' l'errore lo spiega il sistema.
                        const std::wstring wide(1, character);
                        const std::string  utf8 = ToUtf8(wide);
                        if (app->renameSelectEnd > 0)
                        {
                            // La selezione e' un **prefisso**: si sostituisce lei, e
                            // quello che segue (l'estensione) resta dov'e'.
                            app->renameText = utf8 + app->renameText.substr(app->renameSelectEnd);
                            app->renameSelectEnd = 0;
                            app->renameCaret     = utf8.size();
                        }
                        else
                        {
                            app->renameText.insert(app->renameCaret, utf8);
                            app->renameCaret += utf8.size();
                        }
                    }
                    else if (app->optionEditActive)
                    {
                        // Il campo di testo di un'opzione: il carattere va nel
                        // nome, non nel filtro del pannello che lo contiene.
                        const std::wstring wide(1, character);
                        app->optionEditText += ToUtf8(wide);
                    }
                    else if (app->searchFieldActive)
                    {
                        // Il campo della ricerca prende i caratteri prima del filtro: e'
                        // un campo di testo, e chi l'ha acceso sta scrivendo li'.
                        app->searchText.push_back(character);
                    }
                    else if (GroupMenu* menu = OpenMenu(*app))
                    {
                        GroupMenuChar(*menu, character);
                    }
                    else if (app->contextMenu.open)
                    {
                        // Il menu contestuale e' modale come quelli a gruppi: il
                        // carattere e' testo da cercare fra le sue voci, non l'inizio
                        // del filtro della lista che sta sotto.
                        ContextMenuChar(*app, character);
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
            // La finestra dell'aggiornamento e' modale anche coi tasti di sistema:
            // `Alt+F4` mentre si installa chiuderebbe il programma a meta' operazione.
            if (app != nullptr && UpdateDialogVisible(*app))
                return 0;

            // La cattura prende anche i tasti di sistema: `Alt` li rende tali, e senza
            // questo `Alt+qualcosa` non si potrebbe assegnare — e `Alt+F4` chiuderebbe
            // la finestra mentre si sta scegliendo un tasto.
            if (app != nullptr && app->keyCaptureCommand >= 0)
            {
                KeyMapKeyDown(*app, wparam);
                return 0;
            }

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
            // Alt+Su sale di un livello, e da una radice di volume porta a "This
            // PC". Arriva come tasto di sistema come Alt+freccia laterale.
            if (app != nullptr && wparam == VK_UP)
            {
                RunCommand(*app, CmdGoUp);
                return 0;
            }
            break;

        case WM_SYSCHAR:
            // `Alt`+lettera produce anche questo, oltre a `WM_SYSKEYDOWN`: senza
            // rispondere, la cattura di un tasto con `Alt` suonerebbe.
            if (app != nullptr && app->keyCaptureCommand >= 0)
                return 0;
            break;

    }

    return kMessageNotHandled;
}
} // namespace Bolt
