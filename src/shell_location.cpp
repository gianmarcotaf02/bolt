#include "shell_location.h"

#include <cstring>

#include "app_ui.h"
#include "language.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <propkey.h>
// `PSGetPropertyKeyFromName`: le due proprieta' del cestino non stanno in `propkey.h` e
// si chiedono per nome canonico (`RecycleFacts`).
#include <propsys.h>
#include <propvarutil.h>

namespace ShellLocation
{
namespace
{

// Il CLSID di una posizione, se ne ha uno: "::{645FF040-...}" e "::{645FF040-...}\..."
// cominciano tutti e due con un CLSID.
std::wstring ClsidOf(const std::wstring& location)
{
    if (!IsNamespace(location))
        return std::wstring();
    const size_t open = location.find(L'{');
    if (open == std::wstring::npos)
        return std::wstring();
    const size_t close = location.find(L'}', open);
    if (close == std::wstring::npos)
        return std::wstring();
    return location.substr(open, close - open + 1);
}

bool ReadRegistryString(HKEY key, const wchar_t* name, std::wstring* out)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t))
        return false;

    std::wstring raw(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(raw.data()),
                         &bytes) != ERROR_SUCCESS)
        return false;
    raw.resize(wcslen(raw.c_str()));
    *out = raw;
    return !out->empty();
}

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), length,
                        nullptr, nullptr);
    return out;
}

// La dimensione e la data come le da' la shell: `GetDetailsEx` e' quello che usa
// la lista di Esplora risorse per riempire le colonne, quindi la data di una voce
// del cestino e' quella che il cestino mostra — la data di eliminazione.
// VARIANT e non PROPVARIANT: qui e' `IShellFolder2::GetDetailsEx` a scrivere, e
// scrive un VARIANT. Piu' sotto, per le proprieta' che legge `IPropertyStore`, il
// tipo e' l'altro — sono due strade diverse con due tipi diversi.
uint64_t DetailBytes(IShellFolder2* folder, PCUITEMID_CHILD child, const PROPERTYKEY& key)
{
    VARIANT value{};
    VariantInit(&value);
    if (FAILED(folder->GetDetailsEx(child, &key, &value)))
        return 0;

    ULONGLONG bytes = 0;
    const bool ok = SUCCEEDED(VariantToUInt64(value, &bytes));
    VariantClear(&value);
    return ok ? static_cast<uint64_t>(bytes) : 0;
}

bool DetailTime(IShellFolder2* folder, PCUITEMID_CHILD child, const PROPERTYKEY& key, FILETIME* out)
{
    VARIANT value{};
    VariantInit(&value);
    if (FAILED(folder->GetDetailsEx(child, &key, &value)))
        return false;

    FILETIME time{};
    const bool ok = SUCCEEDED(VariantToFileTime(value, PSTF_UTC, &time));
    VariantClear(&value);
    if (ok)
        *out = time;
    return ok;
}

} // namespace

// Il volume di un percorso: e' la radice di un volume? Che file system ha, e quanto
// spazio. `Volume` sta nell'intestazione perche' lo legge anche chi decide se una
// voce della lista e' un'unita'.
Volume VolumeOf(const std::wstring& location)
{
    Volume info;
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
        info.fileSystem = Bolt::ToUtf8(fileSystem);

    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    if (GetDiskFreeSpaceExW(location.c_str(), &available, &total, nullptr) && total.QuadPart > 0)
    {
        info.freeText  = Bolt::HumanBytes(available.QuadPart);
        info.totalText = Bolt::HumanBytes(total.QuadPart);
        info.usedFraction = 1.0f - static_cast<float>(static_cast<double>(available.QuadPart) /
                                                     static_cast<double>(total.QuadPart));
    }
    return info;
}
namespace
{
// Il prefisso di una posizione codificata da un PIDL: `shell:pidl:` + esadecimale.
constexpr const wchar_t* kPidlPrefix = L"shell:pidl:";
}   // namespace

bool IsNamespace(const std::wstring& location)
{
    // Tre forme, e sono tre cose diverse: `::{CLSID}` cerca una cartella dentro lo
    // spazio dei nomi del desktop, `shell:::{CLSID}` la chiede alla shell per
    // identificatore — l'unica che risponde per "Accesso rapido" — e `shell:pidl:`
    // e' un PIDL codificato (vedi `EncodePidl`), cioe' una posizione che per nome
    // non si puo' nominare.
    if (location.size() >= 2 && location[0] == L':' && location[1] == L':')
        return true;
    if (location.rfind(kPidlPrefix, 0) == 0)
        return true;
    return location.rfind(L"shell::", 0) == 0;
}

std::wstring EncodePidl(const std::vector<BYTE>& pidl)
{
    static const wchar_t* digits = L"0123456789abcdef";
    std::wstring encoded = kPidlPrefix;
    encoded.reserve(encoded.size() + pidl.size() * 2);
    for (BYTE byte : pidl)
    {
        encoded.push_back(digits[byte >> 4]);
        encoded.push_back(digits[byte & 0x0F]);
    }
    return encoded;
}

PIDLIST_ABSOLUTE Parse(const std::wstring& location)
{
    if (location.rfind(kPidlPrefix, 0) == 0)
    {
        const size_t start = std::wstring(kPidlPrefix).size();
        const std::wstring hex = location.substr(start);
        if (hex.empty() || (hex.size() % 2) != 0)
            return nullptr;

        const auto digit = [](wchar_t c) -> int
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };

        std::vector<BYTE> bytes;
        bytes.reserve(hex.size() / 2);
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            const int hi = digit(hex[i]);
            const int lo = digit(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return nullptr;
            bytes.push_back(static_cast<BYTE>((hi << 4) | lo));
        }
        bytes.push_back(0);
        bytes.push_back(0);   // il PIDL finisce con due byte a zero

        auto* pidl = static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(bytes.size()));
        if (pidl == nullptr)
            return nullptr;
        std::memcpy(pidl, bytes.data(), bytes.size());
        return pidl;
    }

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &pidl, 0, nullptr)))
        return nullptr;
    return pidl;
}

std::wstring DisplayName(const std::wstring& location)
{
    PIDLIST_ABSOLUTE pidl = Parse(location);
    if (pidl == nullptr)
        return std::wstring();

    // Il nome di una cartella di spazio dei nomi **non** si chiede all'oggetto:
    // per "Questo PC" l'oggetto risponde col nome del computer, che non e' una
    // cartella e non e' un'informazione da mostrare in giro.
    //
    // 1. Una cartella di spazio dei nomi ha un CLSID, e il suo nome sta la':
    //    `HKCR\CLSID\{...}\LocalizedString` e' la stringa indiretta che la shell
    //    scrive accanto all'icona, tradotta — `@…windows.storage.dll,-9216` per
    //    "Questo PC". E' lo stesso meccanismo delle etichette del menu "Nuovo".
    const std::wstring clsid = ClsidOf(location);
    if (!clsid.empty())
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (L"CLSID\\" + clsid).c_str(), 0, KEY_READ, &key) ==
            ERROR_SUCCESS)
        {
            std::wstring raw;
            if (ReadRegistryString(key, L"LocalizedString", &raw) ||
                ReadRegistryString(key, nullptr, &raw))
            {
                if (!raw.empty() && raw[0] == L'@')
                {
                    std::vector<wchar_t> buffer(512);
                    if (SUCCEEDED(SHLoadIndirectString(raw.c_str(), buffer.data(),
                                                       static_cast<UINT>(buffer.size()), nullptr)))
                    {
                        RegCloseKey(key);
                        return buffer.data();
                    }
                }
                else if (!raw.empty())
                {
                    RegCloseKey(key);
                    return raw;
                }
            }
            RegCloseKey(key);
        }
    }

    // 2. Le voci **dentro** una cartella di spazio dei nomi non hanno un CLSID.
    //    Per quelle valgono, in ordine, il nome che scrive la shell accanto
    //    all'icona e quello che le da' la cartella che le contiene.
    std::wstring out;

    SHFILEINFOW info{};
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &info, sizeof(info),
                       SHGFI_PIDL | SHGFI_DISPLAYNAME) != 0 &&
        info.szDisplayName[0] != L'\0')
        out = info.szDisplayName;

    PIDLIST_ABSOLUTE up = out.empty() ? ILClone(pidl) : nullptr;
    if (up != nullptr && ILRemoveLastID(up))
    {
        IShellFolder* parent = nullptr;
        if (SUCCEEDED(SHBindToObject(nullptr, up, nullptr, IID_IShellFolder,
                                     reinterpret_cast<void**>(&parent))) &&
            parent != nullptr)
        {
            PCUITEMID_CHILD child = ILFindLastID(pidl);
            STRRET strret{};
            wchar_t buffer[512]{};
            if (SUCCEEDED(parent->GetDisplayNameOf(child, SHGDN_NORMAL, &strret)) &&
                SUCCEEDED(StrRetToBufW(&strret, child, buffer, 512)))
                out = buffer;
            parent->Release();
        }
    }
    if (up != nullptr)
        CoTaskMemFree(up);

    // Ultimo ripiego: il nome che l'oggetto da' di se'.
    if (out.empty())
    {
        PWSTR name = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) && name != nullptr)
        {
            out = name;
            CoTaskMemFree(name);
        }
    }

    CoTaskMemFree(pidl);
    return out;
}

std::wstring ParsingNameOf(REFKNOWNFOLDERID id)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetKnownFolderIDList(id, 0, nullptr, &pidl)) || pidl == nullptr)
        return std::wstring();

    PWSTR name = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &name)) && name != nullptr)
    {
        out = name;
        CoTaskMemFree(name);
    }
    CoTaskMemFree(pidl);
    return out;
}

std::wstring QuickAccessName()
{
    // **L'unico identificatore scritto nel codice**, e vale la pena dire perche'.
    // "Accesso rapido" ha un CLSID e una chiave in `HKCR\CLSID`, ma **non** una voce
    // in `FolderDescriptions`: `SHGetKnownFolderIDList` non lo conosce, e la strada
    // che funziona per il cestino e per "Questo PC" qui restituisce vuoto.
    // La forma con `shell::` e non `::` da sola: la prima chiede alla shell la
    // cartella *per identificatore*, la seconda la cerca **dentro** lo spazio dei
    // nomi del desktop, dove "Accesso rapido" non sta. Con `::{...}` la shell
    // risponde "non lo so", che e' quello che si e' misurato.
    const std::wstring location = L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}";

    // Il prefisso diverso si porta dietro una conseguenza: `IsNamespace` guarda il
    // nome, quindi deve riconoscere anche questa forma.


    // Si verifica che la shell lo riconosca. Un nome di spazio dei nomi sbagliato
    // non da' errore: da' una cartella **vuota**, che e' la forma di guasto peggiore
    // — sembra che non ci sia niente da mostrare, invece di sembrare un guasto.
    PIDLIST_ABSOLUTE pidl = Parse(location);
    if (pidl == nullptr)
        return std::wstring();

    CoTaskMemFree(pidl);
    return location;
}

std::wstring ParentOf(const std::wstring& location)
{
    PIDLIST_ABSOLUTE pidl = Parse(location);
    if (pidl == nullptr)
        return std::wstring();

    // Per una posizione codificata il genitore si ricodifica: il suo *nome* e' il
    // percorso del file riciclato, che riporterebbe fuori dallo spazio dei nomi.
    const bool encoded = location.rfind(kPidlPrefix, 0) == 0;
    std::wstring parent;
    PIDLIST_ABSOLUTE up = ILClone(pidl);
    if (up != nullptr && ILRemoveLastID(up))
    {
        if (encoded)
        {
            const size_t size = ILGetSize(up);
            parent = EncodePidl(std::vector<BYTE>(reinterpret_cast<const BYTE*>(up),
                                                  reinterpret_cast<const BYTE*>(up) + size));
        }
        else
        {
            PWSTR name = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList(up, SIGDN_DESKTOPABSOLUTEPARSING, &name)) &&
                name != nullptr)
            {
                parent = name;
                CoTaskMemFree(name);
            }
        }
    }
    if (up != nullptr)
        CoTaskMemFree(up);
    CoTaskMemFree(pidl);
    return parent;
}

std::vector<Column> Columns(const std::vector<BYTE>& pidl)
{
    std::vector<Column> columns;
    if (pidl.empty())
        return columns;

    PCUIDLIST_ABSOLUTE absolute = reinterpret_cast<PCUIDLIST_ABSOLUTE>(pidl.data());
    IShellFolder* folder = nullptr;
    PCUITEMID_CHILD child = nullptr;
    if (FAILED(SHBindToParent(absolute, IID_IShellFolder, reinterpret_cast<void**>(&folder), &child)) ||
        folder == nullptr || child == nullptr)
        return columns;

    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_IShellFolder2, reinterpret_cast<void**>(&folder2));

    if (folder2 != nullptr)
    {
        // Quante colonne: si va avanti finche' la shell da' un nome. Non c'e' un
        // "quante sono", e chiederlo a un indice di troppo risponde con un nome vuoto
        // invece che con un errore.
        for (int index = 0; index < 24; ++index)
        {
            SHELLDETAILS title{};
            if (FAILED(folder2->GetDetailsOf(nullptr, index, &title)))
                break;

            wchar_t titleText[256]{};
            if (FAILED(StrRetToBufW(&title.str, nullptr, titleText, 256)) || titleText[0] == L'\0')
                break;

            // Il nome della voce lo si mostra gia': ripeterlo in una riga "Name" e'
            // una riga in piu' con dentro la stessa parola.
            if (_wcsicmp(titleText, L"Name") == 0 || _wcsicmp(titleText, L"Nome") == 0)
                continue;

            SHELLDETAILS details{};
            if (FAILED(folder2->GetDetailsOf(child, index, &details)))
                continue;

            wchar_t valueText[512]{};
            if (FAILED(StrRetToBufW(&details.str, child, valueText, 512)) || valueText[0] == L'\0')
                continue;

            Column column;
            column.title = titleText;
            column.value = valueText;
            columns.push_back(std::move(column));
        }
        folder2->Release();
    }

    folder->Release();
    return columns;
}

// Le due cose che solo il cestino sa di una voce riciclata: quando e' stata buttata e
// da dove veniva.
//
// Non sono proprieta' di **sistema** e non stanno in `propkey.h`: le conosce la cartella
// del cestino, che le espone come colonne. Chiederle per indice di colonna vorrebbe dire
// indovinare l'ordine di una tabella i cui **nomi** sono tradotti, e su una macchina in
// un'altra lingua l'indovinello cambierebbe risposta. Si chiedono quindi per nome
// canonico, che la lingua non la tocca: `System.Recycle.DateDeleted` e
// `System.Recycle.DeletedFrom`.
//
// Verificato il 2026-10-21 con `PSGetPropertyKeyFromName`: quei due nomi si risolvono
// (`9B174B33-3` e `9B174B33-2`), mentre `System.DateDeleted` e
// `System.Recycle.OriginalLocation` **non esistono** — una funzione che li avesse usati
// avrebbe risposto sempre «non lo so» senza dire perche'.
//
// Falso se la voce non e' una voce riciclata: fuori dal cestino i due campi restano
// vuoti, e la colonna scrive `--` invece di inventare una data.
bool RecycleFacts(const std::vector<BYTE>& pidl, FILETIME* deleted, std::string* originalLocation)
{
    if (deleted != nullptr)
        *deleted = FILETIME{};
    if (originalLocation != nullptr)
        originalLocation->clear();
    if (pidl.empty())
        return false;

    PCUIDLIST_ABSOLUTE absolute = reinterpret_cast<PCUIDLIST_ABSOLUTE>(pidl.data());
    IShellFolder* folder = nullptr;
    PCUITEMID_CHILD child = nullptr;
    if (FAILED(SHBindToParent(absolute, IID_IShellFolder, reinterpret_cast<void**>(&folder), &child)) ||
        folder == nullptr || child == nullptr)
        return false;

    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_IShellFolder2, reinterpret_cast<void**>(&folder2));
    folder->Release();
    if (folder2 == nullptr)
        return false;

    // Una proprieta' per nome canonico. `GetDetailsEx` risponde con una VARIANT vuota
    // per una proprieta' che il folder non conosce — non e' un errore, e' «non lo so».
    const auto fetch = [folder2, child](const wchar_t* name, VARIANT* out) -> bool
    {
        PROPERTYKEY key{};
        if (FAILED(PSGetPropertyKeyFromName(name, &key)))
            return false;
        VariantInit(out);
        if (FAILED(folder2->GetDetailsEx(child, &key, out)) || out->vt == VT_EMPTY)
        {
            VariantClear(out);
            return false;
        }
        return true;
    };

    bool found = false;

    VARIANT when;
    if (fetch(L"System.Recycle.DateDeleted", &when))
    {
        FILETIME stamp{};
        // In **UTC**: `FormatFileTime` converte in locale da sola, e convertirla due
        // volte sposterebbe la data di un'ora o di un giorno a seconda dell'ora legale.
        if (SUCCEEDED(VariantToFileTime(when, PSTF_UTC, &stamp)) && deleted != nullptr)
        {
            *deleted = stamp;
            found = true;
        }
        VariantClear(&when);
    }

    VARIANT from;
    if (fetch(L"System.Recycle.DeletedFrom", &from))
    {
        // La shell la da' come stringa, ma non c'e' un tipo solo per «stringa» in una
        // VARIANT: `VariantToString` li copre tutti, e per un valore che non sa
        // convertire risponde con un errore invece di scrivere niente.
        wchar_t buffer[512] = {};
        const bool text = SUCCEEDED(VariantToString(from, buffer, 512)) && buffer[0] != L'\0';
        if (text && originalLocation != nullptr)
        {
            *originalLocation = ToUtf8(std::wstring(buffer));
            found = true;
        }
        VariantClear(&from);
    }

    folder2->Release();
    return found;
}

// La voce sta dentro il **cestino**? Il PIDL del cestino si chiede una volta sola.
//
// Serve a una distinzione che il resto non sa fare: una cartella aperta da
// `ACCESSO RAPIDO` e una voce riciclata hanno tutte e due un PIDL che non e' quello
// del desktop, eppure la prima **e'** la cartella che il suo nome dice, la seconda
// no (il suo nome e' il blob dentro `$Recycle.Bin`, un'altra voce). Dentro il
// cestino resta la regola di prima — il PIDL, e la posizione codificata per le
// cartelle — perche' li' l'identita' vera e' quella della shell.
bool InsideRecycleBin(PCUIDLIST_ABSOLUTE item)
{
    static PIDLIST_ABSOLUTE bin = []() -> PIDLIST_ABSOLUTE {
        PIDLIST_ABSOLUTE out = nullptr;
        if (FAILED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &out)))
            return nullptr;
        return out;
    }();
    if (bin == nullptr || item == nullptr)
        return false;
    return ILIsParent(bin, item, FALSE) != FALSE;
}

std::vector<Item> Read(const std::wstring& location, std::string* error)
{
    std::vector<Item> items;
    error->clear();

    PIDLIST_ABSOLUTE root = Parse(location);
    if (root == nullptr)
    {
        *error = Tr("Questa posizione non si apre.", "This location cannot be opened.");
        return items;
    }

    IShellFolder* folder = nullptr;
    if (FAILED(SHBindToObject(nullptr, root, nullptr, IID_IShellFolder,
                              reinterpret_cast<void**>(&folder))) ||
        folder == nullptr)
    {
        *error = Tr("Questa posizione non si legge.", "This location cannot be read.");
        CoTaskMemFree(root);
        return items;
    }

    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_IShellFolder2, reinterpret_cast<void**>(&folder2));

    IEnumIDList* enumerator = nullptr;
    const HRESULT enumerated =
        folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumerator);
    if (FAILED(enumerated) || enumerator == nullptr)
    {
        if (folder2 != nullptr)
            folder2->Release();
        folder->Release();
        CoTaskMemFree(root);
        *error = Tr("Questa posizione non si elenca.", "This location cannot be listed.");
        return items;
    }

    PITEMID_CHILD child = nullptr;
    ULONG fetched = 0;
    while (enumerator->Next(1, &child, &fetched) == S_OK && fetched == 1)
    {
        Item item;

        // Il nome come si vede. `SHGDN_INFOLDER` e' quello che scrive la shell
        // accanto all'icona — per una voce del cestino e' il nome che il file
        // aveva prima, non il nome interno `$R...`.
        STRRET display{};
        wchar_t buffer[512]{};
        if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_INFOLDER, &display)))
        {
            if (SUCCEEDED(StrRetToBufW(&display, child, buffer, 512)))
                item.name = buffer;
        }

        PCUITEMID_CHILD relative = child;
        SFGAOF attributes = SFGAO_FOLDER;
        if (FAILED(folder->GetAttributesOf(1, &relative, &attributes)))
            attributes = 0;
        item.isDirectory = (attributes & SFGAO_FOLDER) != 0;

        // Il nome assoluto serve per navigarci dentro e per i verbi della shell:
        // senza, una voce del cestino non si potrebbe ne' aprire ne' ripristinare.
        PIDLIST_ABSOLUTE absolute = ILCombine(root, child);
        if (absolute != nullptr)
        {
            PWSTR name = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList(absolute, SIGDN_DESKTOPABSOLUTEPARSING, &name)) &&
                name != nullptr)
            {
                item.location = name;
                CoTaskMemFree(name);
            }
        }

        // Il PIDL, copiato: la lista se lo porta dietro, e serve a chiedere alla
        // shell i verbi *di questa voce* — e' cosi' che si ripristina dal cestino.
        //
        // `absolute` si libera **alla fine** di questo blocco, e non prima: qui
        // sotto lo si legge ancora due volte — `ILGetSize` per sapere quanto e'
        // lungo, e `ILIsEqual` per sapere se il nome torna su questa voce. Liberarlo
        // nella prima `if` lasciava il pidl in mano all'allocatore e il resto leggeva
        // memoria gia' restituita: il pidl copiato poteva essere un blocco riusato, e
        // con lui i verbi della shell (il ripristino dal cestino) ricevevano
        // un'identita' che non era quella della voce.
        if (absolute != nullptr)
        {
            const size_t size = ILGetSize(absolute);
            item.pidl.assign(reinterpret_cast<const BYTE*>(absolute),
                             reinterpret_cast<const BYTE*>(absolute) + size);

            // E il nome torna su questa voce, o su un'altra?
            PIDLIST_ABSOLUTE reparsed = Parse(item.location);
            if (reparsed != nullptr)
            {
                item.navigable = ILIsEqual(absolute, reparsed) != FALSE;
                CoTaskMemFree(reparsed);
            }

            // `ILIsEqual` confronta la **catena di PIDL**, non il percorso: una voce
            // dentro `ACCESSO RAPIDO` (o un'unita' dentro "Questo PC") ha per genitore
            // il PIDL della sua cartella, mentre il nome assoluto riparte dal desktop.
            // Sono due catene diverse per la **stessa** cartella, e il confronto diceva
            // di no: la voce diventava una posizione codificata, e li' i comandi per
            // percorso si rifiutavano (`docs/02` §7.64).
            //
            // Fuori dal cestino c'e' una domanda migliore, che non guarda le catene:
            // il percorso che la shell da' per questa voce **e'** il nome con cui la
            // si e' trovata? Se si', aprire quel nome apre questa voce. Dentro il
            // cestino no: li' il nome e' il blob, e la regola resta quella di prima.
            if (!item.navigable && !item.location.empty() && !InsideRecycleBin(absolute))
            {
                PWSTR fileSystemPath = nullptr;
                if (SUCCEEDED(SHGetNameFromIDList(absolute, SIGDN_FILESYSPATH, &fileSystemPath)) &&
                    fileSystemPath != nullptr)
                {
                    if (_wcsicmp(fileSystemPath, item.location.c_str()) == 0)
                        item.navigable = true;
                    CoTaskMemFree(fileSystemPath);
                }
            }

            // Una **cartella** che per nome non si ritrova — dentro il cestino il
            // nome assoluto di una cartella riciclata e' il file riciclato, che e'
            // un'altra cosa — si apre lo stesso, col PIDL: la posizione diventa
            // `shell:pidl:<esadecimale>` e da li' in poi e' una posizione come le
            // altre (`Parse` la sa leggere). Era il pezzo che mancava per aprire una
            // cartella dal cestino invece di doverla solo ripristinare.
            if (!item.navigable && item.isDirectory)
            {
                item.location  = EncodePidl(item.pidl);
                item.navigable = true;
            }
            CoTaskMemFree(absolute);
        }

        item.name = item.name.empty() ? item.location : item.name;
        item.extension = [&item]() {
            const size_t dot = item.name.rfind(L'.');
            if (item.isDirectory || dot == std::wstring::npos || dot + 1 >= item.name.size())
                return std::string();
            std::string extension = ToUtf8(item.name.substr(dot + 1));
            for (char& letter : extension)
                letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
            return extension;
        }();

        if (folder2 != nullptr)
        {
            item.bytes = DetailBytes(folder2, child, PKEY_Size);
            DetailTime(folder2, child, PKEY_DateModified, &item.modified);
            DetailTime(folder2, child, PKEY_DateCreated, &item.created);
            DetailTime(folder2, child, PKEY_DateAccessed, &item.accessed);
        }

        items.push_back(std::move(item));
        CoTaskMemFree(child);
        child = nullptr;
    }

    enumerator->Release();
    if (folder2 != nullptr)
        folder2->Release();
    folder->Release();
    CoTaskMemFree(root);
    return items;
}

} // namespace ShellLocation
