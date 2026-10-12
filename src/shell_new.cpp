#include "shell_new.h"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

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
// "@C:\WINDOWS\system32\zipfldr.dll,-10194" dove noi vogliamo "Nuova cartella
// compressa", e la traduzione sta in quella DLL, non nel nostro eseguibile.
//
// Si risolve con `SHLoadIndirectString`, non con `RegLoadMUIString`: la seconda,
// provata sulla stessa stringa, restituisce un errore — e il ripiego che ne segue
// mostra il nome del tipo in INGLESE mentre tutto il resto del menu e' in
// italiano. La funzione di shlwapi e' quella che usa la shell, e dà la stringa
// localizzata.
bool ReadLabel(HKEY key, const wchar_t* name, std::wstring* out)
{
    std::wstring raw;
    if (!ReadRaw(key, name, &raw) || raw.empty())
        return false;

    if (raw[0] == L'@')
    {
        std::vector<wchar_t> buffer(512);
        if (FAILED(SHLoadIndirectString(raw.c_str(), buffer.data(),
                                        static_cast<UINT>(buffer.size()), nullptr)))
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

// "Nuova cartella compressa.zip" e "Nuovo documento di testo.txt": il nome che
// propone la shell. Le etichette che sono gia' un nome — cominciano per "Nuov" —
// si usano cosi' come sono, o verrebbe "Nuovo nuova cartella compressa"; le
// altre prendono il prefisso e la prima lettera minuscola, perche' "Nuovo
// Documento di testo" non lo scrive nessuno.
std::wstring SuggestName(const std::wstring& label, const std::wstring& extension)
{
    std::wstring name = label;
    if (_wcsnicmp(label.c_str(), L"Nuov", 4) != 0)
    {
        name = L"Nuovo " + label;
        if (name.size() > 6 && name[6] >= L'A' && name[6] <= L'Z')
            name[6] = static_cast<wchar_t>(name[6] - L'A' + L'a');
    }
    return name + extension;
}

// Il file modello: un percorso assoluto, oppure un nome dentro la cartella
// `ShellNew` di Windows. Il registro usa tutte e due le forme.
std::wstring ResolveTemplatePath(const std::wstring& value)
{
    if (value.size() > 2 && value[1] == L':')
        return value;

    wchar_t directory[MAX_PATH]{};
    if (GetWindowsDirectoryW(directory, MAX_PATH) == 0)
        return value;
    return std::wstring(directory) + L"\\ShellNew\\" + value;
}

// Il nome del tipo di un'estensione, tradotto: e' la stessa fonte con cui la
// lista riempie la colonna "Tipo", quindi "Documento di testo" e' lo stesso
// testo nei due posti. `USEFILEATTRIBUTES` chiede il nome SENZA toccare il disco:
// il file non esiste, e non deve esistere.
std::wstring TypeNameOf(const std::wstring& extension)
{
    SHFILEINFOW info{};
    const std::wstring probe = L"nuovo" + extension;
    if (SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) == 0)
        return std::wstring();
    return info.szTypeName;
}

bool ReadData(HKEY key, std::vector<BYTE>* out)
{
    DWORD type  = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"Data", nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes == 0)
        return false;

    // `Data` e' documentato come binario, ma il registro usa anche la stringa:
    // il modello di `.rtf` e' `{\rtf1}` scritto come `REG_SZ`, e i suoi sette byte
    // sono esattamente il contenuto del file nuovo. Si prendono i byte della
    // stringa, senza il terminatore — quello non fa parte del file.
    if (type == REG_SZ || type == REG_EXPAND_SZ)
    {
        std::wstring text(bytes / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key, L"Data", nullptr, nullptr, reinterpret_cast<BYTE*>(text.data()),
                             &bytes) != ERROR_SUCCESS)
            return false;
        text.resize(wcslen(text.c_str()));
        if (text.empty())
            return false;

        const int needed = WideCharToMultiByte(CP_ACP, 0, text.c_str(),
                                               static_cast<int>(text.size()), nullptr, 0, nullptr,
                                               nullptr);
        out->assign(static_cast<size_t>(needed), 0);
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                            reinterpret_cast<char*>(out->data()), needed, nullptr, nullptr);
        return !out->empty();
    }

    if (type != REG_BINARY)
        return false;

    out->assign(bytes, 0);
    return RegQueryValueExW(key, L"Data", nullptr, nullptr, out->data(), &bytes) == ERROR_SUCCESS;
}

// Un modello, se la chiave e' davvero un modello.
void AddTemplate(HKEY shellNew, const std::wstring& progId, const std::wstring& extension,
                 bool noExtension, std::vector<Template>* out)
{
    // Il contenuto, nell'ordine in cui la shell li guarda. `Command` non e' un
    // modo di creare un file ma di avviare un programma, quindi resta fuori: vedi
    // l'intestazione di `shell_new.h`.
    Template item;
    item.extension = noExtension ? std::wstring() : extension;

    std::wstring templateFile;
    if (ReadRaw(shellNew, L"FileName", &templateFile) && !templateFile.empty())
    {
        item.source       = Template::Source::TemplateFile;
        item.templatePath = ResolveTemplatePath(templateFile);
    }
    else if (ReadData(shellNew, &item.data))
    {
        item.source = Template::Source::Data;
    }
    else if (HasValue(shellNew, L"NullFile"))
    {
        item.source = Template::Source::Empty;
    }
    else
    {
        return;   // `Command`, o niente che il sistema sappia usare
    }

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

    item.label         = label;
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

    // Due tipi che la shell conosce **da sé e non registra**: "Documento di testo"
    // e "Immagine bitmap" stanno nel menu "Nuovo" di Esplora risorse, ma nessuna
    // chiave `ShellNew` di questa macchina li nomina — sono 37, e `.txt` e `.bmp`
    // non ci sono. Senza queste due voci il menu perderebbe proprio il tipo che si
    // crea piu' spesso.
    for (const wchar_t* extension : {L".txt", L".bmp"})
    {
        const std::wstring label = TypeNameOf(extension);
        if (label.empty())
            continue;

        Template item;
        item.label         = label;
        item.extension     = extension;
        item.source        = Template::Source::Empty;   // un file vuoto
        item.suggestedName = SuggestName(label, item.extension);

        bool duplicate = false;
        for (const Template& existing : out)
            duplicate = duplicate || existing.suggestedName == item.suggestedName;
        if (!duplicate)
            out.push_back(std::move(item));
    }

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
