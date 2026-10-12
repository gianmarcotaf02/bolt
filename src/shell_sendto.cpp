#include "shell_sendto.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

namespace ShellSendTo
{
namespace
{

// Il nome senza estensione. La shell mostra "Cartella compressa" per
// "Cartella compressa.lnk": l'estensione del collegamento non interessa a
// nessuno, e mostrarla vorrebbe dire spiegare a schermo cos'e' un .lnk.
std::wstring Stem(const std::wstring& name)
{
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
        return name;
    return name.substr(0, dot);
}

std::vector<Target> ReadAll()
{
    std::vector<Target> out;

    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_SendTo, 0, nullptr, &folder)))
        return out;
    const std::wstring directory = folder;
    CoTaskMemFree(folder);

    WIN32_FIND_DATAW entry{};
    const std::wstring pattern = directory + L"\\*";
    HANDLE search = FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE)
        return out;

    do
    {
        // `.`, `..` e i file di servizio nascosti (il `desktop.ini` della
        // cartella): nel menu della shell non si vedono, e non si vedono qui.
        if (entry.cFileName[0] == L'.')
            continue;
        if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0)
            continue;

        Target target;
        target.label = Stem(entry.cFileName);
        target.path  = directory + L"\\" + entry.cFileName;
        out.push_back(std::move(target));
    } while (FindNextFileW(search, &entry));

    FindClose(search);

    // Ordine di etichetta, come per il menu "Nuovo": il file system lo restituisce
    // in ordine di nome completo, e un menu che cambia ordine non si impara.
    std::sort(out.begin(), out.end(), [](const Target& a, const Target& b) {
        return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0;
    });
    return out;
}

} // namespace

std::vector<Target> Targets()
{
    // Deliberatamente non in cache: e' una cartella dell'utente, e chi ci mette
    // dentro un collegamento nuovo si aspetta di vederlo. Vedi la testa del file.
    return ReadAll();
}

} // namespace ShellSendTo
