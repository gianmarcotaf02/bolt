// ---------------------------------------------------------------------------
// I comandi: gli effetti.
//
// Il punto in cui un gesto diventa un effetto e' `RunCommand`: un tasto, una voce di
// un menu, una riga della palette e un clic finiscono tutti li', e da li' parte
// l'azione. I `Do*` sono le azioni sui file — copia, incolla, elimina, cartella
// nuova — e la rinomina in linea.
//
// Uscito da `main.cpp` il 2026-10-18. E' l'ultimo blocco dello spezzamento che era
// anche il primo della lista del lavoro, perche' **M4 passa di qui** (`Cancella
// recenti`, i menu dei recenti): spostare un pezzo mentre ci si lavora costa meno
// che spostarlo dopo.
//
// Il confine: qui c'e' **cosa** succede, non **dove si vede**. Le superfici dei menu
// e il pannello delle proprieta' stanno ancora in `main.cpp` e si chiamano da qui
// attraverso `app_actions.h` — la cucitura dichiarata. Un comando chiede a una
// superficie di aprirsi; disegnarla e' un altro mestiere.
// ---------------------------------------------------------------------------
#include "commands.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "app_actions.h"
#include "app_ui.h"
#include "diag.h"
#include "file_ops.h"
#include "home.h"
#include "language.h"
#include "navigation.h"
#include "settings.h"
#include "shell_location.h"
#include "shell_new.h"
#include "theme.h"
#include "view_context_menu.h"
#include "view_list.h"
#include "view_options.h"
#include "view_statusbar.h"

namespace Bolt
{

// I comandi che agiscono su un **percorso** non valgono dove un percorso non c'e':
// nel cestino le voci si spostano, si cancellano e si ripristinano coi verbi della
// shell, che quegli oggetti li conoscono. Qui si dice, invece di far finta che sia
// un guasto e lasciare l'utente a chiedersi perche' non succede niente.
//
// La Home e' il secondo caso, e per una ragione diversa: le sue righe **nominano**
// cartelle vere. Senza questa guardia `Elimina` su una riga della Home
// cancellerebbe la cartella che quella riga nomina, e l'utente non sta guardando
// quella cartella: sta guardando un elenco di destinazioni.
static bool RefuseInHome(AppState& app, const std::wstring& action)
{
    if (!IsHome(app.directory))
        return false;
    ReportStatus(app, action +
                          TrW(L": dalla Home non si toccano i file. Apri la cartella "
                              L"che vuoi cambiare.",
                              L": files are not changed from Home. Open the folder "
                              L"you want to change."));
    return true;
}

bool RefuseInNamespace(AppState& app, const std::wstring& action)
{
    if (RefuseInHome(app, action))
        return true;
    if (!ShellLocation::IsNamespace(app.directory))
        return false;
    ReportStatus(app, action +
                          TrW(L": qui non si puo', questa cartella non e' un percorso. "
                              L"Le voci si gestiscono coi comandi del menu contestuale.",
                              L": not here, this folder is not a path. "
                              L"Use the context menu commands for these items."));
    return true;
}

// Nome predefinito di una cartella nuova, come quello di Esplora risorse.
// Il nome di partenza di una cartella nuova: e' l'unico nome di file che scriviamo
// noi, quindi segue la lingua dell'interfaccia, come fa Esplora risorse.
std::wstring NewFolderName() { return TrW(L"Nuova cartella", L"New folder"); }

void BeginRename(AppState& app)
{
    if (RefuseInNamespace(app, L"Rinomina"))
        return;

    const int index = CursorIndex(app);
    if (index < 0)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(index)];
    app.renameActive   = true;
    app.renamePath     = JoinPath(app.directory, entry.wideName);
    app.renameOriginal = entry.wideName;
    app.renameText     = entry.name;

    // Come Esplora risorse: il campo si apre col nome **selezionato fino
    // all'estensione** — per una cartella, tutto il nome. Scrivere sopra sostituisce
    // la selezione, e il `.txt` resta. La selezione e' una posizione in byte, non un
    // cursore: il campo non si sposta, e non serve.
    app.renameSelectEnd = app.renameText.size();
    if (!entry.isDirectory)
    {
        const size_t dot = app.renameText.rfind('.');
        if (dot != std::string::npos && dot > 0)
            app.renameSelectEnd = dot;
    }
    app.renameCaret = app.renameSelectEnd;   // si scrive **prima** dell'estensione

    // La tastiera adesso scrive nel nome, non nel filtro: senza questo, rinominare
    // in "foto" filtrerebbe la lista a ogni lettera.
    app.filterActive = false;
}

void CancelRename(AppState& app)
{
    app.renameActive = false;
    app.renameText.clear();
    app.renameSelectEnd = 0;
    app.renameCaret = 0;
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
        ReportStatus(app, TrW(L"Rinominato in ", L"Renamed to ") + landed);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, TrW(L"Rinomina non riuscita: ", L"Rename failed: ") + error);
    }
}

void DoCopyToClipboard(AppState& app, bool cut)
{
    // Dalla Home la copia e' rifiutata come le altre operazioni: quello che si
    // vedrebbe copiato e' la cartella **vera** che la riga nomina, e non e' quello
    // che l'utente sta guardando. Le voci di spazio dei nomi restano com'erano:
    // li' il rifiuto lo danno i verbi della shell e non sta a noi cambiarlo.
    if (RefuseInHome(app, cut ? L"Taglia" : L"Copia"))
        return;

    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    if (!FileOps::SetClipboardFiles(app.window, paths, cut))
    {
        ReportStatus(app, TrW(L"Clipboard non disponibile.", L"Clipboard unavailable."));
        return;
    }

    app.clipboardCut = cut;
    ReportStatus(app, VerbItems(paths.size(),
                                cut ? L"Tagliato " : L"Copiato ",
                                cut ? L"Tagliati " : L"Copiati ",
                                cut ? L"Cut " : L"Copied "));
}

void DoPaste(AppState& app)
{
    if (RefuseInNamespace(app, L"Incolla"))
        return;

    std::vector<std::wstring> paths;
    bool cut = false;
    if (!FileOps::ReadClipboardFiles(&paths, &cut))
    {
        ReportStatus(app, TrW(L"Niente da incollare.", L"Nothing to paste."));
        return;
    }

    // Nel registro, perche' una volta un incolla e' partito senza che si riuscisse a
    // capire da quale gesto: senza una riga, l'unica traccia era un messaggio di
    // stato che dura quattro secondi.
    DiagLog("  incolla: " + std::to_string(paths.size()) + " percorsi, " +
            (cut ? "tagliati" : "copiati") + ", in " + ToUtf8(LeafName(app.directory)));

    // L'operazione adesso e' lunga: si avvia sul suo thread e l'esito si raccoglie
    // quando finisce, in `FinishOperation`. Qui si dice solo che e' partita — o che e'
    // in coda dietro un'altra, che dal punto di vista di chi ha premuto e' lo stesso:
    // sara' fatta.
    FileOps::Request request;
    request.action      = cut ? FileOps::Action::Move : FileOps::Action::Copy;
    request.sources     = std::move(paths);
    request.destination = app.directory;
    BeginOperation(app, std::move(request), OperationSource::Command);
}

void DoDelete(AppState& app, bool permanently)
{
    if (RefuseInNamespace(app, L"Elimina"))
        return;

    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    // Anche l'eliminazione e' lunga — un albero di cartelle intero — e passa dallo
    // stesso pannello della copia. Quello che si butta via e' quello che era
    // selezionato adesso, non quello della clipboard.
    FileOps::Request request;
    request.action  = permanently ? FileOps::Action::DeleteForever : FileOps::Action::Recycle;
    request.sources = paths;
    BeginOperation(app, std::move(request), OperationSource::Command);
}

void DoNewFolder(AppState& app)
{
    if (RefuseInNamespace(app, L"Nuova cartella"))
        return;

    const float scroll = app.scroll;
    std::vector<std::wstring> created;
    std::wstring error;
    const std::wstring wanted = NewFolderName();
    const FileOps::Result result = FileOps::CreateFolder(
        app.window, app.directory, wanted.c_str(), &created, &error);

    if (result == FileOps::Result::Ok)
    {
        const std::wstring name = created.empty() ? wanted : LeafName(created.front());
        std::unordered_set<std::wstring> keep;
        keep.insert(name);
        ReloadKeeping(app, keep, name, scroll);

        // Creare e rinominare sono un'azione sola: chi preme Ctrl+Shift+N vuole
        // dare un nome, non guardare un'altra "Nuova cartella".
        BeginRename(app);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, TrW(L"Cartella non creata: ", L"Folder not created: ") + error);
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
            ReportStatus(app, TrW(L"Il modello di questo tipo non si e' potuto copiare: "
                                  L"il file e' stato creato vuoto.",
                                  L"The template for this type could not be copied: "
                                  L"the file was created empty."));
            return;
        }
    }
    else
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            ReportStatus(app, TrW(L"Il file e' stato creato, ma il contenuto del modello no.",
                                  L"The file was created, but not the template content."));
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
            ReportStatus(app, TrW(L"Il file e' stato creato, ma il contenuto del modello no.",
                                  L"The file was created, but not the template content."));
            return;
        }
    }

    // Il modello c'era ma era vuoto: il file e' nato vuoto, e chi lo apre lo deve
    // sapere adesso e non quando l'applicazione gli dira' che e' danneggiato.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow == 0)
    {
        ReportStatus(app, TrW(L"Il modello di questo tipo e' vuoto: ",
                              L"The template for this type is empty: ") +
                              LeafName(path) + TrW(L" e' stato creato vuoto.",
                                                   L" was created empty."));
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
        ReportStatus(app, L"\"" + entry.wideName +
                              TrW(L"\" non si apre da qui: si ripristina.",
                                  L"\" does not open from here: restore it."));
    }
    else if (!entry.location.empty())
    {
        // Un file dentro il cestino non si apre: si ripristina o si elimina, e
        // quei comandi sono del menu contestuale della shell. Dirlo e' meglio che
        // far finta di averlo aperto.
        ReportStatus(app, L"\"" + entry.wideName +
                              TrW(L"\" non si apre da qui: usa il menu contestuale.",
                                  L"\" does not open from here: use the context menu."));
    }
    else
    {
        // Un file aperto e' un recente anche lui: peso 1, non 3 — la
        // navigazione pesa 3 (`FUN_14007fa10` passa 1 per l'apertura).
        app.recents.Touch(full, 1, /*isDirectory=*/false);
        ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// Crea una voce nuova dal modello |index| del registro, col meccanismo del
// sistema: il nome definitivo lo sceglie la shell ("Nuovo documento di testo.txt",
// e "(2)" se esiste gia'), il contenuto lo mette `FillNewItemContent`.
void CreateNewItem(AppState& app, size_t index)
{
    // Il menu spegne gia' "Nuovo file" dove non vale; questa e' la stessa regola
    // scritta anche qui, perche' la funzione non deve poter essere chiamata per
    // sbaglio da un posto che non spegne niente.
    if (RefuseInNamespace(app, L"Nuovo file"))
        return;

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
            ReportStatus(app, TrW(L"File non creato: ", L"File not created: ") + error);
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
        //
        // Da una **radice di volume** invece `ParentOf` non sale: `C:\` e' padre di
        // se stesso, e salire da li' non faceva niente. Sopra un disco c'e' "This
        // PC", che e' una cartella di spazio dei nomi: la stessa lista di unita'
        // della barra laterale, raggiungibile col gesto che ci si aspetta.
        case CmdGoUp:
            if (ShellLocation::IsNamespace(app.directory))
            {
                const std::wstring up = ShellLocation::ParentOf(app.directory);
                if (!up.empty())
                    LoadDirectory(app, up);
            }
            else
            {
                const std::wstring up = ParentOf(app.directory);
                if (up != app.directory)
                    LoadDirectory(app, up);
                else if (!app.thisPcPath.empty() && app.directory != app.thisPcPath)
                    LoadDirectory(app, app.thisPcPath);
            }
            break;
        case CmdBack:      NavigateBack(app); break;
        case CmdForward:   NavigateForward(app); break;
        case CmdGoTo:      BuildGoToMenu(app); break;
        case CmdPalette:   BuildPaletteMenu(app); break;
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
                // Il percorso della riga lo sa `EntryLocation`: sulla Home il nome
                // della riga e' un'identita', non una coda da attaccare alla
                // cartella aperta.
                const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
                const std::wstring full = EntryLocation(app, entry);
                const std::wstring arguments = L"/select,\"" + full + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
            }
            else if (IsHome(app.directory))
            {
                // La Home non e' una cartella da aprire: senza una riga scelta non
                // c'e' niente da mostrare in Esplora risorse.
                ReportStatus(app, TrW(L"Scegli una riga: la Home non e' una cartella",
                                      L"Choose a row: Home is not a folder"));
            }
            else
            {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", app.directory.c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;

        case CmdOpenConsole:
            if (RefuseInHome(app, L"Apri un terminale"))
                break;
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
        case CmdToggleShowRecents:      app.showRecents      = !app.showRecents;      RememberSettings(app); break;
        // Riordina subito: l'opzione si vede nella lista, quindi la lista deve
        // gia' essere nell'ordine nuovo quando il pannello si chiude.
        case CmdToggleSortRecents:      app.sortRecentsOnTop = !app.sortRecentsOnTop; SortEntries(app); app.viewDirty = true; RememberSettings(app); break;
        // `Toggle Subfolders`: ricaricare e' l'unico modo onesto di cambiare modo. Le
        // voci in piu' stanno su disco, non in memoria: tenerne un secondo elenco da
        // parte per toglierle senza rileggere vorrebbe dire un elenco che si
        // disallinea al primo `F5`.
        case CmdToggleSubfolders:
            app.showSubfolders = !app.showSubfolders;
            LoadDirectory(app, app.directory, /*recordHistory=*/false);
            ReportStatus(app, app.showSubfolders
                                  ? TrW(L"Sottocartelle nella lista", L"Subfolders in the list")
                                  : TrW(L"Sottocartelle fuori dalla lista",
                                        L"Subfolders off the list"));
            RememberSettings(app);
            break;
        case CmdSaveLayout:
        {
            // Il layout e' la **forma** della finestra, e si salva da dove si sta
            // guardando: nessuna domanda, perche' la risposta e' gia' sullo schermo.
            AppState::SavedLayout& layout = app.savedLayout;
            layout.defined        = true;
            layout.viewMode       = static_cast<int>(app.viewMode);
            layout.previewStep    = app.previewStep;
            layout.zoom           = app.zoom;
            layout.sortColumn     = static_cast<int>(app.sortColumn);
            layout.sortAscending  = app.sortAscending;
            layout.groupMode      = static_cast<int>(app.groupMode);
            layout.sidebarVisible = app.sidebarVisible;
            layout.showSubfolders = app.showSubfolders;
            layout.columns        = app.columns;
            RememberSettings(app);
            ReportStatus(app, TrW(L"Layout salvato", L"Layout saved"));
            break;
        }
        case CmdLoadLayout:
        {
            if (!app.savedLayout.defined)
            {
                ReportStatus(app, TrW(L"Nessun layout salvato: usa 'Salva il layout' prima",
                                      L"No saved layout: use 'Save layout' first"));
                break;
            }
            const AppState::SavedLayout& layout = app.savedLayout;
            const int modes = static_cast<int>(ViewMode::Icons) + 1;
            app.viewMode      = static_cast<ViewMode>(std::clamp(layout.viewMode, 0, modes - 1));
            app.previewStep   = std::clamp(layout.previewStep, 0, AppState::kPreviewStepCount - 1);
            app.zoom          = layout.zoom;
            app.sortColumn    = static_cast<SortColumn>(layout.sortColumn);
            app.sortAscending = layout.sortAscending;
            app.groupMode     = static_cast<GroupMode>(layout.groupMode);
            app.sidebarVisible = layout.sidebarVisible;
            // Un layout dice **quali** colonne, non quanto sono larghe: le larghezze
            // salvate in una vista le si ritrovava nell'altra, e due intestazioni una
            // sopra l'altra (`Data modifica` dentro `Cartelle`) sono il risultato. La
            // larghezza si riadatta al contenuto, che e' il valore di partenza di una
            // colonna appena comparsa.
            for (int i = 0; i < kColumnCount; ++i)
                app.columns[static_cast<size_t>(i)].hidden =
                    layout.columns[static_cast<size_t>(i)].hidden;
            app.showCreatedDate  = !layout.columns[static_cast<size_t>(ColumnOfSort(SortColumn::Created))].hidden;
            app.showAccessedDate = !layout.columns[static_cast<size_t>(ColumnOfSort(SortColumn::Accessed))].hidden;
            FitColumns(app);
            app.autoFitColumns = true;
            SortEntries(app);
            app.viewDirty = true;
            // `Toggle Subfolders` cambia cosa c'e' nella lista, e l'unico modo onesto
            // di applicarlo e' rileggere la cartella.
            if (app.showSubfolders != layout.showSubfolders)
            {
                app.showSubfolders = layout.showSubfolders;
                LoadDirectory(app, app.directory, /*recordHistory=*/false);
            }
            ReportStatus(app, TrW(L"Layout caricato", L"Layout loaded"));
            break;
        }
        case CmdForgetLayout:
            app.savedLayout = AppState::SavedLayout{};
            RememberSettings(app);
            ReportStatus(app, TrW(L"Layout dimenticato", L"Layout forgotten"));
            break;
        // Riaprire le schede: e' l'unica preferenza che scrive **quali cartelle** erano
        // aperte. Spegnendola il file viene cancellato subito, non alla prossima uscita.
        case CmdToggleRememberTabs:
            app.rememberTabs = !app.rememberTabs;
            RememberSettings(app);
            SaveSession(app);
            ReportStatus(app, app.rememberTabs
                                  ? TrW(L"Le schede aperte si ritrovano al prossimo avvio",
                                        L"Open tabs will be restored next time")
                                  : TrW(L"Le schede non si ricordano piu'",
                                        L"Open tabs will not be remembered"));
            break;
        case CmdSingleClick:            app.singleClick            = !app.singleClick;            RememberSettings(app); break;
        case CmdSingleClickFoldersOnly: app.singleClickFoldersOnly = !app.singleClickFoldersOnly; RememberSettings(app); break;
        case CmdActOnPress:             app.actOnPress             = !app.actOnPress;             RememberSettings(app); break;
        case CmdDoubleClickToParent:    app.doubleClickToParent    = !app.doubleClickToParent;    RememberSettings(app); break;
        case CmdShowCheckboxes:         app.showCheckboxes         = !app.showCheckboxes;         RememberSettings(app); break;
        // La lingua non e' solo un testo da ridisegnare: le intestazioni dei gruppi
        // sono un valore **costruito** (`RebuildView`) e i nomi di tipo li ha
        // tradotti la shell e li teniamo in memoria, quindi vanno rifatti. Senza
        // questo la lista resta nella lingua di prima e sembra che il comando non
        // abbia fatto niente.
        case CmdLanguageItalian:
            gLanguage = Language::Italian;
            for (Entry& entry : app.entries)
                entry.typeText = ShellTypeName(entry.extension, entry.isDirectory);
            app.viewDirty = true;
            RememberSettings(app);
            ReportStatus(app, TrW(L"Impostazioni salvate in ", L"Settings saved to ") + SettingsPath());
            break;
        case CmdLanguageEnglish:
            gLanguage = Language::English;
            for (Entry& entry : app.entries)
                entry.typeText = ShellTypeName(entry.extension, entry.isDirectory);
            app.viewDirty = true;
            RememberSettings(app);
            ReportStatus(app, TrW(L"Impostazioni salvate in ", L"Settings saved to ") + SettingsPath());
            break;
        // La pagina di avvio. Due comandi espliciti e non una casella da riempire a
        // mano: la cartella che si vuole all'avvio e' quasi sempre **questa**, e la
        // seconda risposta e' tornare al comportamento di partenza.
        case CmdStartupCurrentFolder:
        {
            // Uno spazio dei nomi (il cestino, "Questo PC") o la Home non sono un
            // percorso e non si possono salvare come cartella di avvio: la
            // preferenza vuota significa Home, e una riga della Home non e' la Home.
            if (ShellLocation::IsNamespace(app.directory) || IsHome(app.directory))
            {
                ReportStatus(app, TrW(L"Questa posizione non si puo' scegliere come avvio",
                                      L"This location cannot be the startup folder"));
                break;
            }
            app.startupFolder = app.directory;
            RememberSettings(app);
            ReportStatus(app, TrW(L"All'avvio si aprira' ", L"Startup folder: ") + app.directory);
            break;
        }
        case CmdStartupThisPc:
            // "Questo PC" adesso si **scrive** nella preferenza invece di svuotarla:
            // vuota vuol dire Home, che e' il valore di partenza nuovo. Il nome e'
            // quello di spazio dei nomi, che la navigazione sa gia' aprire.
            app.startupFolder = app.thisPcPath;
            RememberSettings(app);
            ReportStatus(app, TrW(L"All'avvio si aprira' Questo PC",
                                  L"Startup folder: This PC"));
            break;

        // Un controllo esplicito: qui si fa partire, e non si aspetta (vedi `update.*`).
        case CmdCheckUpdates: CheckForUpdates(app); break;

        case CmdShowSettingsPath:
            ReportStatus(app, SettingsPath().empty()
                                 ? TrW(L"Impostazioni non disponibili", L"Settings unavailable")
                                 : TrW(L"Impostazioni in ", L"Settings in ") + SettingsPath());
            break;
        // Le tre dell'aspetto, che sono anche le tre chiavi dello spec. Sono la via
        // rapida per cambiare tavolozza: dal pannello delle opzioni servono tre passaggi
        // — aprire, cercare, scegliere — e qui uno.
        case CmdColorSchemeLight:
            SetColorScheme(app, ColorScheme::Light);
            ReportStatus(app, TrW(L"Tema: chiaro", L"Theme: light"));
            break;
        case CmdColorSchemeDark:
            SetColorScheme(app, ColorScheme::Dark);
            ReportStatus(app, TrW(L"Tema: scuro", L"Theme: dark"));
            break;
        case CmdSystemColorScheme:
            app.systemColorScheme = !app.systemColorScheme;
            ApplyTheme(app);
            RememberSettings(app);
            ReportStatus(app, app.systemColorScheme
                                 ? TrW(L"Tema: segue il sistema", L"Theme: follows the system")
                                 : TrW(L"Tema: scelto a mano", L"Theme: chosen by hand"));
            break;
        case CmdOpenOptions:
            BuildOptionsMenu(app);
            break;
        case CmdEditKeyMap:
            BuildKeyMapMenu(app);
            break;
        case CmdPinTo:   BuildPinMenu(app, /*removing=*/false); break;
        // Il layout di **questa** cartella: si salva quello che si vede adesso —
        // vista, misura, ordinamento, gruppi, larghezze — e si ritrova entrando.
        case CmdSaveFolderLayout:
            SaveFolderLayout(app);
            ReportStatus(app, TrW(L"Layout di questa cartella salvato.",
                                  L"This folder's layout is saved."));
            break;
        case CmdLoadFolderLayout:
            // "Load Layout" del riferimento: rimettere la vista che questa cartella
            // ricorda. Non e' la stessa cosa di "ricorda": quello c'e' gia', e vale
            // entrando. Questo serve dopo aver cambiato vista e voler tornare
            // indietro senza rientrare.
            ReportStatus(app, ApplyFolderLayout(app)
                                ? TrW(L"Layout di questa cartella applicato.",
                                      L"This folder's layout is applied.")
                                : TrW(L"Questa cartella non ha un layout salvato.",
                                      L"This folder has no saved layout."));
            break;
        case CmdForgetFolderLayout:
            ForgetFolderLayout(app);
            ReportStatus(app, TrW(L"Layout di questa cartella dimenticato.",
                                  L"This folder's layout is forgotten."));
            break;
        case CmdUnpin:   BuildPinMenu(app, /*removing=*/true);  break;
        // Togliere **una** voce dai recenti, invece di azzerarli tutti: e' il gesto
        // che manca quando una cartella sbagliata e' finita nell'elenco.
        case CmdForgetRecent:
        {
            const std::wstring& path =
                app.contextTargetPath.empty() ? app.directory : app.contextTargetPath;
            app.recents.Forget(path);
            ReportStatus(app, TrW(L"Tolta dai recenti.", L"Removed from recents."));
            if (IsHome(app.directory))
                LoadDirectory(app, app.directory, /*recordHistory=*/false);
            break;
        }
        case CmdProperties:
            if (RefuseInHome(app, L"Proprietà"))
                break;
            OpenPropertiesPanel(app);
            break;
        case CmdPropertiesWindows:
        {
            if (RefuseInHome(app, L"Proprietà"))
                break;
            const std::vector<std::wstring> paths = SelectedPaths(app);
            if (paths.empty() || ShowProperties(app.window, paths) == 0)
                ReportStatus(app, TrW(L"Proprietà non disponibili per la selezione.",
                                      L"Properties unavailable for the selection."));
            break;
        }
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
        // La griglia dietro l'anteprima dell'immagine: si vede dove finisce lo sfondo
        // dell'immagine e comincia quello del pannello. Si ricorda, come le altre
        // preferenze.
        case CmdToggleImageGrid:
            app.inspectorHideGrid = !app.inspectorHideGrid;
            RememberSettings(app);
            break;
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
        case CmdClearRecents:
            app.recents.Clear();
            // La Home mostra i recenti in una banda: se e' aperta, quella banda
            // adesso e' cambiata.
            if (IsHome(app.directory))
                LoadDirectory(app, app.directory, /*recordHistory=*/false);
            break;

        case CmdSortName:     app.sortColumn = SortColumn::Name;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortType:     app.sortColumn = SortColumn::Type;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortModified: app.sortColumn = SortColumn::Modified; SortEntries(app); app.viewDirty = true; break;
        case CmdSortCreated:  app.sortColumn = SortColumn::Created;  SortEntries(app); app.viewDirty = true; break;
        case CmdSortAccessed: app.sortColumn = SortColumn::Accessed; SortEntries(app); app.viewDirty = true; break;
        case CmdSortTotal:    app.sortColumn = SortColumn::TotalItems; SortEntries(app); app.viewDirty = true; break;
        case CmdSortFolders:  app.sortColumn = SortColumn::Folders;  SortEntries(app); app.viewDirty = true; break;
        case CmdSortFiles:    app.sortColumn = SortColumn::Files;    SortEntries(app); app.viewDirty = true; break;
        case CmdSortSize:     app.sortColumn = SortColumn::Size;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortDirection: app.sortAscending = !app.sortAscending; SortEntries(app); app.viewDirty = true; break;

        // Sulla Home il raggruppamento e' per banda e non si cambia: un comando che
        // lo cambia senza che si veda niente e' peggio di un comando assente.
        case CmdGroupNone:
        case CmdGroupName:
        case CmdGroupDate:
        case CmdGroupSize:
        case CmdGroupCount:
        case CmdGroupVolume:
            if (IsHome(app.directory))
            {
                ReportStatus(app, TrW(L"La Home raggruppa per banda",
                                      L"Home groups by band"));
                break;
            }
            app.groupMode = (command == CmdGroupNone)   ? GroupMode::None
                            : (command == CmdGroupName) ? GroupMode::Name
                            : (command == CmdGroupDate) ? GroupMode::Date
                            : (command == CmdGroupSize) ? GroupMode::Size
                            : (command == CmdGroupCount) ? GroupMode::Count
                                                        : GroupMode::Volume;
            app.viewDirty = true;
            break;

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
} // namespace Bolt
