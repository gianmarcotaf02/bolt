// ---------------------------------------------------------------------------
// I verbi della shell: leggere il menu che Windows sa fare, e nient'altro.
//
// La shell sa fare cose che non sono nostre: "Apri con", "Invia a", i verbi delle
// estensioni installate. Non si reimplementa niente: le si chiede l'oggetto di
// interfaccia della selezione, le si fa descrivere il proprio menu dentro un `HMENU`
// che non mostriamo mai, se ne leggono le voci, e se ne esegue una quando l'utente la
// sceglie. Il riferimento fa esattamente questo — `Win_ContextMenuBuild`,
// `P_GetContextMenu`, `P_ContextMenuInvoke`, e la stringa `GCS_VERBW` per leggere i
// verbi.
//
// Questo file e' la **lettura**: i verbi, i loro id riservati, le etichette ridotte ad
// ASCII (l'atlante dei glifi copre il solo ASCII stampabile, e un'etichetta con un
// accento mostrerebbe un buco), la cache per non interrogare la shell a ogni tasto
// destro, e il rilascio degli oggetti COM. Chi **disegna** il menu e chi ne esegue le
// voci sta ancora in `main.cpp`: e' la superficie, ed e' la prossima a uscire.
// ---------------------------------------------------------------------------
#include "shell_menu.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>
#include <vector>

#include "app_ui.h"
#include "diag.h"
#include "language.h"
#include "view_list.h"

namespace Bolt
{
namespace
{
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

// "file pilot" dentro un testo, senza badare alle maiuscole. Non usa `towlower`
// sulle stringhe UTF-8 delle etichette: li' serve il solo ASCII, e le etichette
// arrivano gia' ridotte ad ASCII da `ShellLabelToAscii`.
bool ContainsFold(const wchar_t* haystack, const wchar_t* needle)
{
    for (const wchar_t* start = haystack; *start != L'\0'; ++start)
    {
        const wchar_t* a = start;
        const wchar_t* b = needle;
        while (*a != L'\0' && *b != L'\0' && towlower(*a) == towlower(*b))
        {
            ++a;
            ++b;
        }
        if (*b == L'\0')
            return true;
    }
    return false;
}

bool ContainsFoldAscii(const std::string& haystack, const char* needle)
{
    const size_t n = std::char_traits<char>::length(needle);
    if (n == 0 || haystack.size() < n)
        return false;
    for (size_t i = 0; i + n <= haystack.size(); ++i)
    {
        size_t j = 0;
        while (j < n &&
               static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j]))) ==
                   needle[j])
            ++j;
        if (j == n)
            return true;
    }
    return false;
}

// Le voci dell'applicazione **originale**. Bolt e' un clone di File Pilot, e chi ha
// File Pilot installato se lo ritrova anche dentro il nostro menu contestuale: "Open
// in File Pilot" su una selezione, "File Pilot here" sullo sfondo della cartella. Sono
// comandi di un **altro programma** che aprono la stessa cartella, e dentro un clone si
// leggono come una nostra funzione — che non e'. Non li offriamo: chi vuole File Pilot
// lo apre da File Pilot, e il nostro menu resta il nostro.
//
// Il confronto e' sul **verbo canonico**, che e' il nome stabile: le varianti che
// l'estensione registra portano tutte il nome dell'applicazione dentro
// (`OpenInFilePilot`, `FilePilotHere`), mentre l'etichetta e' tradotta e cambia con la
// lingua di Windows. L'etichetta si guarda lo stesso, come ripiego, per le voci
// ownerdraw che arrivano senza verbo: costa un confronto e copre l'unico caso in cui il
// verbo non c'e'.
bool IsOriginalAppVerb(const wchar_t* verb)
{
    return ContainsFold(verb, L"filepilot") || ContainsFold(verb, L"file pilot");
}

bool LabelMentionsOriginalApp(const std::string& label)
{
    return ContainsFoldAscii(label, "file pilot") || ContainsFoldAscii(label, "filepilot");
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

    // `Windows.Share` **non** e' in questa lista: e' il sottomenu "Dare accesso a",
    // che la shell popola come gli altri e che noi non offriamo con un menu nostro.
    // Prima si saltava, e spariva dal menu contestuale una cosa che il riferimento
    // ha. `Windows.SendTo` si salta perche' "Invia a" lo costruiamo noi
    // (`shell_sendto.*`), e due voci con lo stesso nome sarebbero un doppione.
    static const wchar_t* const kShellOwn[] = {
        L"New",      L"Windows.SendTo", L"SendTo",
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
} // namespace

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

// L'icona che un'applicazione **dichiara** per il proprio verbo: e' un valore `Icon`
// (`"C:\...pp.exe"` o `"C:\...pp.exe",-105`) sotto la chiave del verbo, in uno
// dei posti dove Windows cerca i verbi. Non e' un'ipotesi sul nome dell'applicazione:
// e' quello che l'applicazione ha scritto per farsi disegnare.
//
// Si guardano tutti i posti e si prende il primo che risponde: il verbo che abbiamo
// letto dice *quale* comando e', non da quale radice viene — e la stessa voce puo'
// essere registrata per i file, per le cartelle o per lo sfondo. Sei aperture di
// chiave per voce, e solo all'apertura del menu (`ReadShellMenu` gira una volta per
// livello, e la shell menu cache la tiene per la selezione).
std::wstring VerbIconSpec(const std::wstring& verb)
{
    // `Software\Classes\<radice>\<verbo>`, con un separatore costruito una volta:
    // incollare le barre a mano in sei stringhe letterali e' il modo di sbagliarne
    // una e di non accorgersene, perche' `\s` e `\D` non sono errori per il
    // compilatore.
    const std::wstring classes = L"Software\\Classes\\";
    const wchar_t sep = L'\\';

    static const wchar_t* const kRoots[] = {
        L"*\\shell",
        L"AllFilesystemObjects\\shell",
        L"Directory\\shell",
        L"Directory\\Background\\shell",
        L"Folder\\shell",
        L"Drive\\shell",
    };

    for (const wchar_t* root : kRoots)
    {
        const std::wstring key = classes + root + std::wstring(1, sep) + verb;

        wchar_t value[512]{};
        DWORD bytes = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"Icon", RRF_RT_REG_SZ, nullptr,
                         value, &bytes) == ERROR_SUCCESS && value[0] != 0)
            return value;
        bytes = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Icon", RRF_RT_REG_SZ, nullptr,
                         value, &bytes) == ERROR_SUCCESS && value[0] != 0)
            return value;
    }
    return {};
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

            // `MIIM_BITMAP` costa niente da chiedere e dice una cosa che serve: se la
            // shell (o l'estensione) ha gia' un'icona per questa voce. Le voci che
            // disegnano da sole mettono `HBMMENU_CALLBACK`, che non e' una bitmap.
            wchar_t text[256]{};
            MENUITEMINFOW detailed{};
            detailed.cbSize     = sizeof(detailed);
            detailed.fMask      = MIIM_STRING | MIIM_BITMAP;
            detailed.dwTypeData = text;
            detailed.cch        = 255;
            GetMenuItemInfoW(handle, static_cast<UINT>(i), TRUE, &detailed);

            ContextMenuItem item;
            item.label    = ShellLabelToAscii(text);
            item.command  = static_cast<int>(basic.wID);
            item.disabled = (basic.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;

            // Le voci che portano dentro il nome dell'applicazione di cui questo e'
            // il clone non si offrono: sono comandi di un altro programma.
            if ((haveVerb && IsOriginalAppVerb(verb)) || LabelMentionsOriginalApp(item.label))
            {
                DiagLog("  saltata (comando dell'applicazione originale): '" + item.label +
                        "'" + (haveVerb ? " verbo '" : " verbo '?") +
                        ShellLabelToAscii(verb) + "'");
                ++(*outSkipped);
                continue;
            }

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
                            item.label + "' verbo '" +
                            (haveVerb ? ShellLabelToAscii(verb) : std::string("?")) + "'");
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

            const bool shellBitmap = detailed.hbmpItem != nullptr &&
                                     detailed.hbmpItem != HBMMENU_CALLBACK &&
                                     reinterpret_cast<ULONG_PTR>(detailed.hbmpItem) > 0xFFFF;

            // L'icona: prima quella che la voce porta con se', poi quella che il
            // verbo dichiara nel registro. Se non c'e' nessuna delle due la voce
            // resta senza icona — non si ripiega su un'ipotesi.
            if (shellBitmap)
            {
                item.shellBitmap = detailed.hbmpItem;
                item.iconKey     = ShellLabelToAscii(verb);
            }
            else if (haveVerb)
            {
                item.iconSpec = VerbIconSpec(verb);
            }

            DiagLog("  voce: '" + item.label + "'" + (haveVerb ? " verbo '" : " verbo '?") +
                    ShellLabelToAscii(verb) + "'" + (item.disabled ? " (spenta)" : "") +
                    (shellBitmap ? " [icona della shell]" : ""));
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
} // namespace Bolt
