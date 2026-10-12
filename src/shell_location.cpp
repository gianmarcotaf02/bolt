#include "shell_location.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <propkey.h>
#include <propvarutil.h>

namespace ShellLocation
{
namespace
{

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
    return location.size() >= 2 && location[0] == L':' && location[1] == L':';
}

std::wstring DisplayName(const std::wstring& location)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &pidl, 0, nullptr)) || pidl == nullptr)
        return std::wstring();

    // Tre strade, in ordine, perche' nessuna va bene sempre. Chiedere il nome
    // all'oggetto con `SIGDN_NORMALDISPLAY`, per "Questo PC", restituisce il nome
    // del **computer**: una macchina, non una cartella, e nemmeno un'informazione
    // da mostrare in giro.
    std::wstring out;

    // 1. `SHGetFileInfo` col PIDL: e' il nome che la shell scrive accanto
    //    all'icona, quindi localizzato — "Questo PC", "Cestino".
    SHFILEINFOW info{};
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &info, sizeof(info),
                       SHGFI_PIDL | SHGFI_DISPLAYNAME) != 0 &&
        info.szDisplayName[0] != L'\0')
        out = info.szDisplayName;

    // 2. Il nome che le da' la cartella che la contiene, come per ogni voce di ogni
    //    elenco: `SHGDN_NORMAL` sul genitore.
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

    // 3. Ripiego, se nessuna delle due ha risposto: il nome che l'oggetto da' di
    //    se'.
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

std::vector<Item> Read(const std::wstring& location, std::string* error)
{
    std::vector<Item> items;
    error->clear();

    PIDLIST_ABSOLUTE root = nullptr;
    if (FAILED(SHParseDisplayName(location.c_str(), nullptr, &root, 0, nullptr)) || root == nullptr)
    {
        *error = "This location cannot be opened.";
        return items;
    }

    IShellFolder* folder = nullptr;
    if (FAILED(SHBindToObject(nullptr, root, nullptr, IID_IShellFolder,
                              reinterpret_cast<void**>(&folder))) ||
        folder == nullptr)
    {
        *error = "This location cannot be read.";
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
        *error = "This location cannot be listed.";
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
