#include "shell_new.h"

#include <windows.h>

#include <algorithm>
#include <string.h>

namespace ShellNew
{
namespace
{

std::vector<std::wstring> SubKeys(HKEY key)
{
    std::vector<std::wstring> names;
    for (DWORD index = 0;; ++index)
    {
        wchar_t buffer[512]{};
        DWORD length = 512;
        const LSTATUS status =
            RegEnumKeyExW(key, index, buffer, &length, nullptr, nullptr, nullptr, nullptr);
        if (status != ERROR_SUCCESS)
            break;   // ERROR_NO_MORE_ITEMS e' la fine normale, il resto e' un guasto
        names.emplace_back(buffer);
    }
    return names;
}

bool HasValue(HKEY key, const wchar_t* name)
{
    return RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

// Legge un valore stringa cosi' com'e', senza risolverlo.
bool ReadRaw(HKEY key, const wchar_t* name, std::wstring* out)
{
    DWORD type  = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS)
        return false;
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return false;
    if (bytes < sizeof(wchar_t))
        return false;

    std::wstring raw(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(raw.data()),
                         &bytes) != ERROR_SUCCESS)
        return false;
    raw.resize(wcslen(raw.c_str()));
    *out = raw;
    return true;
}

// Legge un'etichetta, risolvendo le stringhe indirette: la shell registra
// "@C:\WINDOWS\system32\zipfldr.dll,-10194" dove noi vogliamo "Cartella
// compressa", e le traduzioni stanno in quella DLL, non nel nostro eseguibile.
bool ReadLabel(HKEY key, const wchar_t* name, std::wstring* out)
{
    std::wstring raw;
    if (!ReadRaw(key, name, &raw) || raw.empty())
        return false;

    if (raw[0] == L'@')
    {
        std::vector<wchar_t> buffer(512);
        if (RegLoadMUIStringW(key, name, buffer.data(), static_cast<DWORD>(buffer.size()),
                              nullptr, 0, nullptr) != ERROR_SUCCESS)
            return false;   // un'etichetta che non si risolve non si inventa
        *out = buffer.data();
        return !out->empty();
    }

    *out = raw;
    return true;
}

std::wstring DefaultValue(HKEY key)
{
    std::wstring value;
    ReadLabel(key, nullptr, &value);
    return value;
}

// "Nuovo documento di testo.txt": il nome che propone la shell. La prima lettera
// dell'etichetta va in minuscolo — "Nuovo Documento di testo" non lo scrive
// nessuno — e l'estensione si aggiunge, salvo che la chiave dica di non farlo.
std::wstring SuggestName(const std::wstring& label, const std::wstring& extension)
{
    std::wstring name = L"Nuovo " + label;
    if (name.size() > 6 && name[6] >= L'A' && name[6] <= L'Z')
        name[6] = static_cast<wchar_t>(name[6] - L'A' + L'a');
    return name + extension;
}

// Un modello, se la chiave e' davvero un modello.
void AddTemplate(HKEY shellNew, const std::wstring& progId, const std::wstring& extension,
                 bool noExtension, std::vector<Template>* out)
{
    // I valori con cui il sistema sa creare qualcosa. Una chiave `ShellNew` senza
    // nessuno di questi non e' un modello: la shell la ignora, e ignorarla anche
    // noi evita di offrire una voce che non crea niente.
    if (!HasValue(shellNew, L"NullFile") && !HasValue(shellNew, L"FileName") &&
        !HasValue(shellNew, L"Command") && !HasValue(shellNew, L"Data"))
        return;

    std::wstring label;
    if (!ReadLabel(shellNew, L"ItemName", &label))
        ReadLabel(shellNew, L"MenuText", &label);

    // Senza etichetta nella chiave, il nome del tipo sta sul ProgID: e' la sua
    // `FriendlyTypeName`, o il valore predefinito.
    if (label.empty() && !progId.empty())
    {
        HKEY progid = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, progId.c_str(), 0, KEY_READ, &progid) ==
            ERROR_SUCCESS)
        {
            if (!ReadLabel(progid, L"FriendlyTypeName", &label))
            {
                std::wstring raw;
                if (ReadRaw(progid, nullptr, &raw) && !raw.empty() && raw[0] != L'@')
                    label = raw;
            }
            RegCloseKey(progid);
        }
    }

    if (label.empty())
        label = extension;   // meglio l'estensione di un buco

    Template item;
    item.label     = label;
    item.extension = noExtension ? std::wstring() : extension;
    item.suggestedName = SuggestName(label, item.extension);

    for (const Template& existing : *out)
    {
        if (existing.suggestedName == item.suggestedName)
            return;   // due chiavi, un modello solo
    }
    out->push_back(std::move(item));
}

void AddFrom(HKEY parent, const wchar_t* shellNewName, const std::wstring& progId,
             const std::wstring& extension, bool noExtension, std::vector<Template>* out)
{
    HKEY shellNew = nullptr;
    if (RegOpenKeyExW(parent, shellNewName, 0, KEY_READ, &shellNew) != ERROR_SUCCESS)
        return;

    AddTemplate(shellNew, progId, extension, noExtension, out);
    RegCloseKey(shellNew);
}

std::vector<Template> ReadAll()
{
    std::vector<Template> out;

    HKEY classes = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"", 0, KEY_READ, &classes) != ERROR_SUCCESS)
        return out;

    for (const std::wstring& extension : SubKeys(classes))
    {
        if (extension.empty() || extension[0] != L'.')
            continue;

        HKEY extKey = nullptr;
        if (RegOpenKeyExW(classes, extension.c_str(), 0, KEY_READ, &extKey) != ERROR_SUCCESS)
            continue;

        // `Config\NoExtension`: il nome non porta l'estensione.
        bool noExtension = false;
        {
            HKEY config = nullptr;
            if (RegOpenKeyExW(extKey, L"Config", 0, KEY_READ, &config) == ERROR_SUCCESS)
            {
                noExtension = HasValue(config, L"NoExtension");
                RegCloseKey(config);
            }
        }

        // 1. Sotto la chiave dell'estensione, col ProgID di default per il nome.
        const std::wstring defaultProgId = DefaultValue(extKey);
        AddFrom(extKey, L"ShellNew", defaultProgId, extension, noExtension, &out);

        // 2. Sotto un ProgID annidato nella chiave dell'estensione: e' la forma
        //    piu' usata su Windows 11, e senza questa il menu avrebbe un quinto
        //    delle voci che ha quello della shell.
        for (const std::wstring& sub : SubKeys(extKey))
            AddFrom(extKey, (sub + L"\\ShellNew").c_str(), sub, extension, noExtension, &out);

        RegCloseKey(extKey);
    }
    RegCloseKey(classes);

    // Ordine di etichetta: il registro non ne ha uno, e un menu che cambia ordine
    // a ogni avvio non si impara.
    std::sort(out.begin(), out.end(), [](const Template& a, const Template& b) {
        return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0;
    });
    return out;
}

} // namespace

const std::vector<Template>& Templates()
{
    // Una volta sola: sono qualche centinaio di aperture di chiave, e non
    // cambiano mentre il programma gira.
    static const std::vector<Template> templates = ReadAll();
    return templates;
}

} // namespace ShellNew
