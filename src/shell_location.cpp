#include "shell_location.h"

#include "language.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <propkey.h>
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

bool IsNamespace(const std::wstring& location)
{
    // Due forme, e sono due cose diverse: `::{CLSID}` cerca una cartella dentro lo
    // spazio dei nomi del desktop, `shell:::{CLSID}` la chiede alla shell per
    // identificatore — l'unica che risponde per "Accesso rapido".
    if (location.size() >= 2 && location[0] == L':' && location[1] == L':')
        return true;
    return location.rfind(L"shell::", 0) == 0;
}

std::wstring DisplayName(const std::wstring& location)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr)
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
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr)
        return std::wstring();

    CoTaskMemFree(pidl);
    return location;
}

std::wstring ParentOf(const std::wstring& location)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr)
        return std::wstring();

    std::wstring parent;
    PIDLIST_ABSOLUTE up = ILClone(pidl);
    if (up != nullptr && ILRemoveLastID(up))
    {
        PWSTR name = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(up, SIGDN_DESKTOPABSOLUTEPARSING, &name)) && name != nullptr)
        {
            parent = name;
            CoTaskMemFree(name);
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

std::vector<Item> Read(const std::wstring& location, std::string* error)
{
    std::vector<Item> items;
    error->clear();

    PIDLIST_ABSOLUTE root = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &root, 0, nullptr)) || root == nullptr)
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
            PIDLIST_ABSOLUTE reparsed = nullptr;
            if (!item.location.empty() &&
                SUCCEEDED(SHParseDisplayName(item.location.c_str(), nullptr, &reparsed, 0, nullptr)) &&
                reparsed != nullptr)
            {
                item.navigable = ILIsEqual(absolute, reparsed) != FALSE;
                CoTaskMemFree(reparsed);
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
