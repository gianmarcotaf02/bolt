#include "file_ops.h"

// WIN32_LEAN_AND_MEAN esclude shellapi.h da windows.h, ma shlobj.h ha bisogno
// dei tipi che ci stanno dentro: HDROP, DragQueryFileW e i flag FOF_*.
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>

namespace
{
// Formato della clipboard che dice a Esplora risorse se i file vanno copiati o
// spostati. Non e' un CF_* standard: si registra per nome.
constexpr wchar_t kPreferredDropEffect[] = L"Preferred DropEffect";

// Trasforma un HRESULT nel messaggio che Windows stesso mostrerebbe. Un codice
// numerico non dice niente a chi legge il messaggio di errore.
std::wstring DescribeHresult(HRESULT hr)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring text;
    if (length > 0 && buffer != nullptr)
    {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                                 text.back() == L' '))
            text.pop_back();
    }
    if (buffer != nullptr)
        LocalFree(buffer);

    if (text.empty())
    {
        wchar_t fallback[32];
        swprintf_s(fallback, L"0x%08X", static_cast<unsigned>(hr));
        text = fallback;
    }
    return text;
}

// Un oggetto COM con conteggio dei riferimenti, per non dover scrivere
// AddRef/Release in ogni ramo di uscita.
template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr()
    {
        if (ptr_ != nullptr)
            ptr_->Release();
    }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T** operator&() { return &ptr_; }
    T*  operator->() const { return ptr_; }
    T*  Get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

// Sink di avanzamento. Serve per una ragione sola, ma decisiva: quando la shell
// rinomina da sola per evitare un conflitto ("alfa - Copia.txt"), il nome finale
// lo decide lei e IFileOperation non lo comunica. I metodi Post* ricevono invece
// l'elemento appena creato, ed e' l'unico punto in cui quel nome e' osservabile.
//
// Senza, dopo un incolla con conflitto la selezione cade sul file di partenza: il
// cursore finisce sull'originale e un Canc subito dopo cancella la cosa sbagliata.
class OperationSink final : public IFileOperationProgressSink
{
public:
    explicit OperationSink(std::vector<std::wstring>* created) : created_(created) {}

    // --- IUnknown ----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink)
        {
            *out = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG left = InterlockedDecrement(&references_);
        if (left == 0)
            delete this;
        return static_cast<ULONG>(left);
    }

    // --- IFileOperationProgressSink ----------------------------------------
    // Le Pre* e i timer non servono: l'avanzamento lo disegna la shell.
    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT /*result*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT hr,
                                             IShellItem* created) override
    {
        Remember(hr, created);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR,
                                           HRESULT hr, IShellItem* created) override
    {
        Remember(hr, created);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR,
                                           HRESULT hr, IShellItem* created) override
    {
        Remember(hr, created);
        return S_OK;
    }

    // L'eliminazione non produce niente: l'elemento e' andato nel cestino con un
    // nome che non ci serve.
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD,
                                          HRESULT hr, IShellItem* created) override
    {
        Remember(hr, created);
        return S_OK;
    }

private:
    void Remember(HRESULT hr, IShellItem* created)
    {
        if (created_ == nullptr || FAILED(hr) || created == nullptr)
            return;

        LPWSTR path = nullptr;
        if (SUCCEEDED(created->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr)
        {
            created_->push_back(path);
            CoTaskMemFree(path);
        }
    }

    std::vector<std::wstring>* created_ = nullptr;
    LONG references_ = 1;
};

// Costruisce un IShellItem dal percorso. Fallisce sui percorsi che non esistono
// piu', ed e' corretto che fallisca: un'operazione su un file sparito deve
// dirlo, non ignorarlo.
HRESULT ShellItemFromPath(const std::wstring& path, IShellItem** item)
{
    return SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item));
}

// Le opzioni comuni a ogni operazione.
//
// FOF_ALLOWUNDO e' cio' che manda l'eliminazione nel cestino invece di
// distruggere: senza, `Delete` sarebbe irreversibile e la clipboard di sistema
// non potrebbe annullare.
//
// FOFX_ADDUNDORECORD registra l'operazione con Annulla di Windows.
//
// FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR toglie le domande ridondanti.
constexpr DWORD kBaseFlags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD |
                             FOF_NOCONFIRMMKDIR | FOF_NOCONFIRMATION;

// FOF_RENAMEONCOLLISION e' obbligatorio, e la ragione e' un difetto trovato
// provando: senza, con FOF_NOCONFIRMATION attivo, un nome gia' esistente non
// produce ne' una domanda ne' un rinomina automatica. L'operazione viene
// saltata e la shell lascia in giro una finestra di stato modale
// (OperationStatusWindow) che NON si vede, perche' sta dietro la finestra
// principale, ma che tiene l'applicazione bloccata dentro il proprio ciclo di
// messaggi. L'applicazione sembrava congelata, e il difetto sembrava di
// rendering: le catture erano identiche a due secondi di distanza.
//
// Con questa opzione un nome occupato diventa "alfa (2).txt": prevedibile,
// immediato, e senza nessun dialogo da chiudere. Esplora risorse in questo caso
// chiede; noi scegliamo, e lo dichiariamo.
constexpr DWORD kTransferFlags = kBaseFlags | FOF_RENAMEONCOLLISION;

// Prepara IFileOperation, esegue |body|, e traduce l'esito.
//
// L'annullamento non e' un errore: va distinto, perche' l'interfaccia deve
// reagire in modo diverso — un annullamento non si segnala come guasto.
//
// |created| raccoglie i percorsi che l'operazione ha davvero prodotto, nomi
// scelti dalla shell compresi. Puo' essere nullptr.
template <typename Body>
FileOps::Result RunOperation(HWND owner, DWORD flags, std::vector<std::wstring>* created,
                            Body body, std::wstring* error)
{
    ComPtr<IFileOperation> operation;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&operation));
    if (FAILED(hr))
    {
        if (error != nullptr)
            *error = L"Impossibile avviare l'operazione: " + DescribeHresult(hr);
        return FileOps::Result::Failed;
    }

    operation->SetOperationFlags(flags);
    operation->SetOwnerWindow(owner);

    OperationSink* sink = new OperationSink(created);   // il riferimento e' nostro

    hr = body(operation.Get(), sink);
    if (SUCCEEDED(hr))
        hr = operation->PerformOperations();

    // Va rilasciato dopo PerformOperations: il sink deve vivere per tutta
    // l'operazione, che ne tiene una propria referenza.
    sink->Release();

    if (FAILED(hr))
    {
        if (error != nullptr)
            *error = DescribeHresult(hr);
        return FileOps::Result::Failed;
    }

    BOOL aborted = FALSE;
    operation->GetAnyOperationsAborted(&aborted);
    return aborted ? FileOps::Result::Cancelled : FileOps::Result::Ok;
}

// Copia o sposta una lista di origini in una cartella.
FileOps::Result Transfer(HWND owner, const std::vector<std::wstring>& sources,
                         const std::wstring& destination, bool move,
                         std::wstring* error)
{
    if (sources.empty())
        return FileOps::Result::Ok;

    // Una cartella dentro se stessa genererebbe una ricorsione infinita. Il
    // sistema lo intercetta, ma il messaggio che ne esce e' oscuro: meglio non
    // arrivarci.
    for (const std::wstring& source : sources)
    {
        if (destination.size() >= source.size() &&
            _wcsnicmp(destination.c_str(), source.c_str(), source.size()) == 0 &&
            (destination.size() == source.size() ||
             destination[source.size()] == L'\\'))
        {
            if (error != nullptr)
                *error = L"Non si puo' copiare una cartella dentro se stessa.";
            return FileOps::Result::Failed;
        }
    }

    return RunOperation(
        owner, kTransferFlags,
        [&](IFileOperation* operation) -> HRESULT {
            ComPtr<IShellItem> target;
            HRESULT hr = ShellItemFromPath(destination, &target);
            if (FAILED(hr))
                return hr;

            for (const std::wstring& source : sources)
            {
                ComPtr<IShellItem> item;
                hr = ShellItemFromPath(source, &item);
                if (FAILED(hr))
                    return hr;

                hr = move ? operation->MoveItem(item.Get(), target.Get(), nullptr, nullptr)
                          : operation->CopyItem(item.Get(), target.Get(), nullptr, nullptr);
                if (FAILED(hr))
                    return hr;
            }
            return S_OK;
        },
        error);
}
} // namespace

namespace FileOps
{
Result Copy(HWND owner, const std::vector<std::wstring>& sources,
            const std::wstring& destination, std::wstring* error)
{
    return Transfer(owner, sources, destination, /*move=*/false, error);
}

Result Move(HWND owner, const std::vector<std::wstring>& sources,
            const std::wstring& destination, std::wstring* error)
{
    return Transfer(owner, sources, destination, /*move=*/true, error);
}

Result Delete(HWND owner, const std::vector<std::wstring>& paths, bool permanently,
              std::wstring* error)
{
    if (paths.empty())
        return Result::Ok;

    // L'eliminazione definitiva toglie FOF_ALLOWUNDO e aggiunge l'avviso e la
    // conferma: e' l'unica operazione che non si annulla, e deve chiedere.
    const DWORD flags = permanently
                            ? (FOFX_ADDUNDORECORD | FOF_NOCONFIRMMKDIR | FOF_WANTNUKEWARNING)
                            : kBaseFlags;

    return RunOperation(
        owner, flags,
        [&](IFileOperation* operation) -> HRESULT {
            for (const std::wstring& path : paths)
            {
                ComPtr<IShellItem> item;
                HRESULT hr = ShellItemFromPath(path, &item);
                if (FAILED(hr))
                    return hr;
                hr = operation->DeleteItem(item.Get(), nullptr);
                if (FAILED(hr))
                    return hr;
            }
            return S_OK;
        },
        error);
}

Result Rename(HWND owner, const std::wstring& path, const std::wstring& newName,
              std::wstring* error)
{
    return RunOperation(
        owner, kBaseFlags,
        [&](IFileOperation* operation) -> HRESULT {
            ComPtr<IShellItem> item;
            HRESULT hr = ShellItemFromPath(path, &item);
            if (FAILED(hr))
                return hr;
            return operation->RenameItem(item.Get(), newName.c_str(), nullptr);
        },
        error);
}

Result CreateFolder(HWND owner, const std::wstring& parent, const std::wstring& name,
                    std::wstring* created, std::wstring* error)
{
    // NewItem chiede una cartella esistente, quindi il nome "Nuova cartella" puo'
    // gia' esserci: il sistema aggiunge "(2)" da solo.
    const Result result = RunOperation(
        owner, kBaseFlags,
        [&](IFileOperation* operation) -> HRESULT {
            ComPtr<IShellItem> folder;
            HRESULT hr = ShellItemFromPath(parent, &folder);
            if (FAILED(hr))
                return hr;
            return operation->NewItem(folder.Get(), FILE_ATTRIBUTE_DIRECTORY, name.c_str(),
                                      nullptr, nullptr);
        },
        error);

    if (result == Result::Ok && created != nullptr)
    {
        // IFileOperation non dice il nome che ha finito per usare: si legge dal
        // disco. Il chiamante ne ha bisogno per selezionare la cartella nuova.
        *created = parent + L"\\" + name;
        if (GetFileAttributesW(created->c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            // Il nome era occupato: cerco il suffisso che il sistema ha scelto.
            for (int i = 2; i < 1000; ++i)
            {
                std::wstring candidate =
                    parent + L"\\" + name + L" (" + std::to_wstring(i) + L")";
                if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    *created = candidate;
                    break;
                }
            }
        }
    }
    return result;
}

// --- clipboard -------------------------------------------------------------

bool SetClipboardFiles(HWND owner, const std::vector<std::wstring>& paths, bool cut)
{
    if (paths.empty())
        return false;

    // Il blocco della clipboard puo' fallire se un'altra applicazione la sta
    // usando: pochi tentativi, poi si rinuncia invece di restare appesi.
    if (!OpenClipboard(owner))
        return false;

    if (!EmptyClipboard())
    {
        CloseClipboard();
        return false;
    }

    // DROPFILES seguito da un elenco di stringhe wide terminate, chiuso da una
    // stringa vuota. fWide dice al lettore che sono wchar_t e non char.
    size_t characters = 1;
    for (const std::wstring& path : paths)
        characters += path.size() + 1;

    const SIZE_T bytes = sizeof(DROPFILES) + characters * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    bool ok = false;

    if (memory != nullptr)
    {
        if (void* raw = GlobalLock(memory))
        {
            DROPFILES* drop = static_cast<DROPFILES*>(raw);
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;

            wchar_t* cursor = reinterpret_cast<wchar_t*>(static_cast<BYTE*>(raw) +
                                                         sizeof(DROPFILES));
            for (const std::wstring& path : paths)
            {
                memcpy(cursor, path.c_str(), path.size() * sizeof(wchar_t));
                cursor += path.size();
                *cursor++ = L'\0';
            }
            *cursor = L'\0';
            GlobalUnlock(memory);

            if (SetClipboardData(CF_HDROP, memory) != nullptr)
            {
                ok = true;
                memory = nullptr;   // la proprieta' e' passata al sistema
            }
        }
        if (memory != nullptr)
            GlobalFree(memory);
    }

    // L'effetto preferito e' un DWORD sciolto, non una struttura.
    if (ok)
    {
        const UINT format = RegisterClipboardFormatW(kPreferredDropEffect);
        if (format != 0)
        {
            if (HGLOBAL effectMemory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)))
            {
                if (DWORD* effect = static_cast<DWORD*>(GlobalLock(effectMemory)))
                {
                    *effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
                    GlobalUnlock(effectMemory);
                    if (SetClipboardData(format, effectMemory) != nullptr)
                        effectMemory = nullptr;
                }
                if (effectMemory != nullptr)
                    GlobalFree(effectMemory);
            }
        }
    }

    CloseClipboard();
    return ok;
}

bool ClipboardHasFiles()
{
    return IsClipboardFormatAvailable(CF_HDROP) != FALSE;
}

bool ReadClipboardFiles(std::vector<std::wstring>* paths, bool* cut)
{
    if (paths == nullptr || !OpenClipboard(nullptr))
        return false;

    bool ok = false;
    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop != nullptr)
    {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            paths->push_back(path);
        }
        ok = !paths->empty();
    }

    if (cut != nullptr)
    {
        *cut = false;
        const UINT format = RegisterClipboardFormatW(kPreferredDropEffect);
        if (format != 0)
        {
            if (HGLOBAL memory = GetClipboardData(format))
            {
                if (const DWORD* effect = static_cast<const DWORD*>(GlobalLock(memory)))
                {
                    *cut = (*effect & DROPEFFECT_MOVE) != 0;
                    GlobalUnlock(memory);
                }
            }
        }
    }

    CloseClipboard();
    return ok;
}

void ClearClipboard(HWND owner)
{
    if (OpenClipboard(owner))
    {
        EmptyClipboard();
        CloseClipboard();
    }
}
} // namespace FileOps
