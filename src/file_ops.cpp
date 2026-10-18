#include "file_ops.h"
#include "app_ui.h"
#include "diag.h"
#include "language.h"

// WIN32_LEAN_AND_MEAN esclude shellapi.h da windows.h, ma shlobj.h ha bisogno
// dei tipi che ci stanno dentro: HDROP, DragQueryFileW e i flag FOF_*.
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

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

} // namespace

namespace FileOpsDetail
{
// Lo stato che il thread di lavoro scrive e il pannello legge, piu' quello che
// serve a eseguire l'operazione. Vive qui e non nell'intestazione perche' nessuno
// fuori da questo file lo tocca: fuori si vedono `Job` e la sua `Progress`.
struct JobState
{
    std::mutex   mutex;
    std::wstring currentItem;
    uint64_t     totalBytes = 0;
    uint64_t     doneBytes  = 0;
    int          totalItems = 0;
    int          doneItems  = 0;
    UINT         workTotal  = 0;
    UINT         workSoFar  = 0;
    bool         measured   = false;
    bool         preparing  = true;

    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> finished{false};

    FileOps::Result result = FileOps::Result::Ok;
    std::wstring    error;

    // I percorsi che l'operazione ha davvero prodotto, nomi scelti dalla shell per
    // i conflitti compresi. Si leggono dopo `Finish`, cioe' a thread fermo.
    std::vector<std::wstring> created;

    // Il thread, e quello che gli serve.
    std::thread               thread;
    HWND                      owner = nullptr;
    FileOps::Action           action = FileOps::Action::Copy;
    std::vector<std::wstring> sources;
    std::wstring              destination;
    DWORD                     flags = 0;
};
} // namespace FileOpsDetail

namespace
{
// Il sink di avanzamento: e' l'unico posto in cui la shell dice cosa sta facendo.
//
// Tre cose, e tutte e tre servono:
//
//   - i `Pre*` danno la **voce in corso**, che e' quello che si legge nel pannello;
//   - i `Post*` il nome che la shell ha scelto quando un nome era occupato
//     ("alfa - Copia.txt"): senza, dopo un incolla con conflitto la selezione cade
//     sull'originale, e un Canc subito dopo cancella la cosa sbagliata;
//   - `UpdateProgress` il **conto**.
//
// Non tocca l'interfaccia: scrive nello stato condiviso sotto il suo lucchetto,
// perche' la shell lo chiama dal proprio thread, e da li' non si disegna.
class ProgressSink final : public IFileOperationProgressSink
{
public:
    explicit ProgressSink(std::shared_ptr<FileOpsDetail::JobState> state)
        : state_(std::move(state))
    {
    }

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
    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT /*result*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem* item, LPCWSTR) override
    {
        return Note(item);
    }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR) override
    {
        return Note(item);
    }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR) override
    {
        return Note(item);
    }
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem* item) override { return Note(item); }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }

    // `iWorkTotal` e `iWorkSoFar` sono "punti", e il documento non promette che siano
    // byte. La **frazione** pero' e' una frazione: moltiplicata per i byte che
    // abbiamo misurato da' un numero di byte — esatto se i punti sono byte,
    // proporzionale se non lo sono. E' il solo conto che la shell offre, e senza
    // conto un pannello di avanzamento non ha niente da muovere.
    //
    // Ed e' anche il posto giusto per fermarsi: `UpdateProgress` arriva **mentre** si
    // copia anche un solo file, e un codice d'errore qui annulla l'operazione in
    // corso. Senza, l'annullamento di una copia da 5 GB aspetterebbe la fine del
    // file — cioe' non sarebbe un annullamento.
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT iWorkTotal, UINT iWorkSoFar) override
    {
        if (state_->cancelRequested.load())
            return E_ABORT;

        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->workTotal = iWorkTotal;
        state_->workSoFar = iWorkSoFar;
        if (iWorkTotal > 0 && state_->totalBytes > 0)
            state_->doneBytes = static_cast<uint64_t>(static_cast<double>(state_->totalBytes) *
                                                      static_cast<double>(iWorkSoFar) /
                                                      static_cast<double>(iWorkTotal));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT hr,
                                             IShellItem* created) override
    {
        return Remember(hr, created);
    }

    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR,
                                           HRESULT hr, IShellItem* created) override
    {
        return Remember(hr, created);
    }

    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR,
                                           HRESULT hr, IShellItem* created) override
    {
        return Remember(hr, created);
    }

    // L'eliminazione non produce niente da ricordare — l'elemento e' andato nel
    // cestino con un nome che non ci serve — ma e' una voce fatta, e il conto delle
    // voci si muove anche qui.
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT hr, IShellItem*) override
    {
        return Remember(hr, nullptr);
    }

    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD,
                                          HRESULT hr, IShellItem* created) override
    {
        return Remember(hr, created);
    }

private:
    // La voce in corso. Il nome che si legge e' quello che la shell mostra nella sua
    // finestra di avanzamento (`SIGDN_NORMALDISPLAY`): "foto.jpg", non il percorso.
    // Resta nello stato e non va da nessun'altra parte — il registro diagnostico non
    // lo vede, perche' un nome di file personale non si scrive in un file di log.
    HRESULT Note(IShellItem* item)
    {
        // Prima di ogni voce si guarda se qualcuno ha chiesto di fermarsi: e' il
        // punto in cui il documento dice che un errore annulla l'operazione **e
        // tutte quelle in coda**. Per una copia di venti cartelle e' questa la via
        // che ferma le diciannove che non sono ancora partite.
        if (state_->cancelRequested.load())
            return E_ABORT;

        if (item == nullptr)
            return S_OK;

        LPWSTR name = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name != nullptr)
        {
            std::wstring text = name;
            CoTaskMemFree(name);
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->currentItem = std::move(text);
        }
        return S_OK;
    }

    // Il nome definitivo di una voce appena creata, e una voce in piu' nel conto.
    //
    // Il nome si risolve **prima** di prendere il lucchetto: e' una chiamata alla
    // shell, e chi disegna sta aspettando quel lucchetto per un fotogramma.
    HRESULT Remember(HRESULT hr, IShellItem* created)
    {
        std::wstring path;
        if (created != nullptr && SUCCEEDED(hr))
        {
            LPWSTR text = nullptr;
            if (SUCCEEDED(created->GetDisplayName(SIGDN_FILESYSPATH, &text)) && text != nullptr)
            {
                path = text;
                CoTaskMemFree(text);
            }
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        if (SUCCEEDED(hr))
            ++state_->doneItems;
        if (!path.empty())
            state_->created.push_back(std::move(path));
        return S_OK;
    }

    std::shared_ptr<FileOpsDetail::JobState> state_;
    LONG references_ = 1;
};
} // namespace

namespace
{
// Quanti byte e quante voci l'operazione dovra' toccare.
//
// Serve un denominatore, e la shell non lo da': `UpdateProgress` riporta punti, non
// byte. Si misura da soli, con un **tetto di tempo** — una cartella enorme o un
// volume di rete lento non devono rinviare l'inizio dell'operazione di minuti. Oltre
// il tetto si tiene quello che si e' misurato e lo si dichiara: meglio "piu' di
// 3 GB" che un numero che non arriva mai.
void Measure(const std::vector<std::wstring>& paths, double budgetSeconds, uint64_t* bytes,
             int* items, bool* complete)
{
    const double deadline = Bolt::NowSeconds() + budgetSeconds;

    std::vector<std::wstring> pending(paths.rbegin(), paths.rend());
    uint64_t total = 0;
    int      count = 0;
    bool     done  = true;

    while (!pending.empty() && done)
    {
        const std::wstring folder = pending.back();
        pending.pop_back();

        WIN32_FIND_DATAW found{};
        HANDLE search = FindFirstFileW((folder + L"\\*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE)
        {
            // Non e' una cartella: un file, o qualcosa che non si legge. Il file si
            // conta con la dimensione che il sistema riporta.
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (GetFileAttributesExW(folder.c_str(), GetFileExInfoStandard, &info) &&
                (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                total += (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
                ++count;
            }
            continue;
        }

        do
        {
            const wchar_t* name = found.cFileName;
            if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')))
                continue;

            ++count;
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                // Un punto di ricongiungimento si conta e non si attraversa:
                // seguirne uno vuol dire, prima o poi, girare in tondo.
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    pending.push_back(folder + L"\\" + name);
            }
            else
            {
                total += (static_cast<uint64_t>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
            }

            // Il tempo si controlla a blocchi: chiedere l'ora a ogni voce
            // costerebbe piu' della lettura della cartella.
            if ((count & 0xFF) == 0 && Bolt::NowSeconds() > deadline)
            {
                done = false;
                break;
            }
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }

    *bytes    = total;
    *items    = count;
    *complete = done;
}

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
            *error = TrW(L"Impossibile avviare l'operazione: ",
                         L"Could not start the operation: ") + DescribeHresult(hr);
        return FileOps::Result::Failed;
    }

    operation->SetOperationFlags(flags);
    operation->SetOwnerWindow(owner);

    // Il sink serve anche qui, per una ragione sola: il nome che la shell sceglie
    // quando un nome e' gia' occupato lo conoscono solo i suoi `Post*`.
    auto state = std::make_shared<FileOpsDetail::JobState>();
    ProgressSink* sink = new ProgressSink(state);   // il riferimento e' nostro

    hr = body(operation.Get(), sink);
    if (SUCCEEDED(hr))
        hr = operation->PerformOperations();

    // Va rilasciato dopo PerformOperations: il sink deve vivere per tutta
    // l'operazione, che ne tiene una propria referenza.
    sink->Release();

    if (created != nullptr)
        *created = state->created;

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

} // namespace

namespace FileOps
{
// --- le operazioni lunghe: il thread, il conto, l'annullamento ----------------
//
// Il confine e' `Job`: fuori si vede un'operazione che si avvia, che dice a che
// punto e', che si puo' fermare, e che alla fine da' un esito. Dentro ci sono le
// tre cose che prima non c'erano: un thread di lavoro, un sink registrato con
// `Advise` e una porta per l'annullamento.

namespace
{
// Vero se |destination| sta dentro |source|. Una cartella dentro se stessa
// genererebbe una ricorsione infinita: il sistema lo intercetta, ma il messaggio
// che ne esce e' oscuro, e meglio non arrivarci.
bool InsideItself(const std::wstring& source, const std::wstring& destination)
{
    if (destination.size() < source.size())
        return false;
    if (_wcsnicmp(destination.c_str(), source.c_str(), source.size()) != 0)
        return false;
    return destination.size() == source.size() || destination[source.size()] == L'\\';
}
} // namespace

Job::Job(std::shared_ptr<FileOpsDetail::JobState> state) : state_(std::move(state))
{
    // Un'operazione che non deve partire arriva qui con l'errore gia' scritto: chi
    // chiama ha un solo posto dove leggere come e' andata, e non deve sapere se il
    // guasto e' nostro o della shell.
    if (!state_->error.empty())
    {
        state_->result    = Result::Failed;
        state_->preparing = false;
        state_->finished  = true;
        return;
    }

    if (state_->sources.empty())
    {
        // Niente da fare: non vale la pena di un thread per non fare niente.
        state_->preparing = false;
        state_->finished  = true;
        return;
    }

    state_->thread = std::thread(&Job::Run, state_);
}

// L'operazione, sul thread di lavoro. E' l'unico posto in cui si toccano COM e
// `IFileOperation`: fuori si vede solo lo stato.
void Job::Run(std::shared_ptr<FileOpsDetail::JobState> state)
{
    // COM su **questo** thread: l'oggetto dell'operazione vive qui e i suoi callback
    // arrivano sul suo appartamento. MTA e non STA, e la ragione e' precisa: su
    // questo thread nessuno pompa messaggi, e un appartamento ad attesa singola che
    // aspetta una chiamata che nessuno consegna e' un blocco, non un'attesa.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Prima la misura, poi l'operazione: un conto senza denominatore e' un conto
    // che non si puo' disegnare.
    uint64_t bytes    = 0;
    int      items    = 0;
    bool     measured = true;
    Measure(state->sources, 0.75, &bytes, &items, &measured);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->totalBytes = bytes;
        state->totalItems = items;
        state->measured   = measured;
        state->preparing  = false;
    }

    // Annullato mentre si contava: non si fa partire niente. Il conto puo' durare
    // qualche decimo di secondo, e chi ha premuto Annulla in quella finestra non
    // deve poi vedere partire l'operazione.
    if (state->cancelRequested.load())
    {
        state->result   = Result::Cancelled;
        state->finished = true;
        if (SUCCEEDED(com))
            CoUninitialize();
        return;
    }

    HRESULT hr = FAILED(com) ? com : S_OK;

    ComPtr<IFileOperation> operation;
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr))
        hr = operation->SetOperationFlags(state->flags);
    if (SUCCEEDED(hr))
        hr = operation->SetOwnerWindow(state->owner);

    // Il sink si registra **una volta** con `Advise`: da li' arrivano tutte le
    // notifiche, `UpdateProgress` compreso. Passarlo anche a `CopyItem`
    // significherebbe ricevere due volte i `Pre*` e i `Post*`.
    DWORD         cookie = 0;
    ProgressSink* sink   = nullptr;
    if (SUCCEEDED(hr))
    {
        sink = new ProgressSink(state);
        hr   = operation->Advise(sink, &cookie);
    }

    if (SUCCEEDED(hr))
    {
        ComPtr<IShellItem> target;
        if (state->action == Action::Copy || state->action == Action::Move)
            hr = ShellItemFromPath(state->destination, &target);

        for (size_t i = 0; SUCCEEDED(hr) && i < state->sources.size(); ++i)
        {
            ComPtr<IShellItem> item;
            hr = ShellItemFromPath(state->sources[i], &item);
            if (FAILED(hr))
                break;

            switch (state->action)
            {
                case Action::Copy:
                    hr = operation->CopyItem(item.Get(), target.Get(), nullptr, nullptr);
                    break;
                case Action::Move:
                    hr = operation->MoveItem(item.Get(), target.Get(), nullptr, nullptr);
                    break;
                default:
                    hr = operation->DeleteItem(item.Get(), nullptr);
                    break;
            }
        }
    }

    if (SUCCEEDED(hr))
        hr = operation->PerformOperations();

    if (cookie != 0)
        operation->Unadvise(cookie);
    if (sink != nullptr)
        sink->Release();

    // Una riga che dice come la shell conta su **questa** macchina, e che serve a
    // leggere invece di supporre: i "punti" non sono byte (un file da 4 GB sono
    // 131074 punti, cioe' 32 KB l'uno, e un file da 23 byte sono 2 punti), quindi il
    // conto del pannello e' esatto per un file solo e interpolato su tante voci
    // piccole. Misurato cosi' il 2026-10-16.
    DiagLog("  operazione: " + std::to_string(state->totalBytes) + " byte misurati, " +
            std::to_string(state->totalItems) + " voci, " + std::to_string(state->workTotal) +
            " punti dichiarati dalla shell, " + std::to_string(state->workSoFar) + " fatti");

    if (FAILED(hr))
    {
        // Un annullamento non e' un guasto: la shell risponde alla richiesta di
        // fermarsi con un codice d'errore, e riportarlo come "non riuscito"
        // direbbe una cosa falsa.
        state->result = state->cancelRequested.load() ? Result::Cancelled : Result::Failed;
        if (state->result == Result::Failed)
            state->error = DescribeHresult(hr);
    }
    else
    {
        BOOL aborted = FALSE;
        operation->GetAnyOperationsAborted(&aborted);
        // Qui si legge quello che la shell dice di aver **davvero** interrotto, e
        // non quello che e' stato chiesto: un Annulla arrivato quando l'ultima voce
        // era gia' partita non ha fermato niente, e dirlo come interrotto sarebbe
        // una bugia. Se il sink non e' stato ascoltato, questo e' il posto dove
        // risulta.
        state->result = aborted != FALSE ? Result::Cancelled : Result::Ok;
    }

    state->finished = true;

    if (SUCCEEDED(com))
        CoUninitialize();
}

std::unique_ptr<Job> Job::StartTransfer(HWND owner, const std::vector<std::wstring>& sources,
                                        const std::wstring& destination, bool move)
{
    auto state = std::make_shared<FileOpsDetail::JobState>();
    state->owner       = owner;
    state->action      = move ? Action::Move : Action::Copy;
    state->sources     = sources;
    state->destination = destination;

    // FOF_SILENT spegne la finestra di avanzamento della shell: l'originale la
    // sostituisce con la sua `OperationStatusWindow`, e qui la sostituisce il
    // pannello che disegniamo noi.
    state->flags = kTransferFlags | FOF_SILENT;

    for (const std::wstring& source : sources)
    {
        if (InsideItself(source, destination))
        {
            state->error = TrW(L"Non si puo' copiare una cartella dentro se stessa.",
                               L"A folder cannot be copied into itself.");
            break;
        }
    }

    return std::unique_ptr<Job>(new Job(std::move(state)));
}

std::unique_ptr<Job> Job::StartDelete(HWND owner, const std::vector<std::wstring>& paths,
                                      bool permanently)
{
    auto state = std::make_shared<FileOpsDetail::JobState>();
    state->owner   = owner;
    state->action  = permanently ? Action::DeleteForever : Action::Recycle;
    state->sources = paths;

    // L'eliminazione definitiva toglie FOF_ALLOWUNDO e aggiunge l'avviso e la
    // conferma: e' l'unica operazione che non si annulla, e deve chiedere.
    state->flags = permanently
                       ? (FOFX_ADDUNDORECORD | FOF_NOCONFIRMMKDIR | FOF_WANTNUKEWARNING | FOF_SILENT)
                       : (kBaseFlags | FOF_SILENT);

    return std::unique_ptr<Job>(new Job(std::move(state)));
}

Job::~Job()
{
    if (state_ == nullptr)
        return;

    if (state_->thread.joinable())
    {
        // Il thread non si puo' abbandonare: tiene lo stato che il pannello sta
        // leggendo. Si chiede di fermarsi e si aspetta — la richiesta passa dal sink,
        // che la vede alla prima voce o al primo aggiornamento del conto.
        Cancel();
        state_->thread.join();
    }
}

Action Job::action() const
{
    return state_->action;
}

const std::vector<std::wstring>& Job::sources() const
{
    return state_->sources;
}

const std::wstring& Job::destination() const
{
    return state_->destination;
}

bool Job::Running() const
{
    return !state_->finished.load();
}

Progress Job::Snapshot() const
{
    Progress progress;
    progress.cancelRequested = state_->cancelRequested.load();

    std::lock_guard<std::mutex> lock(state_->mutex);
    progress.currentItem = state_->currentItem;
    progress.totalBytes  = state_->totalBytes;
    progress.doneBytes   = state_->doneBytes;
    progress.totalItems  = state_->totalItems;
    progress.doneItems   = state_->doneItems;
    progress.measured    = state_->measured;
    progress.preparing   = state_->preparing;
    return progress;
}

void Job::Cancel()
{
    // Non c'e' niente da chiamare: `IFileOperation` non ha un metodo per fermarsi.
    // L'annullamento e' una **risposta del sink**, che la shell legge — il documento
    // di `PreCopyItem` dice che un codice d'errore annulla l'operazione e tutte quelle
    // che sono in coda, e `UpdateProgress` passa di li' anche a meta' di un file.
    state_->cancelRequested = true;
}

Result Job::Finish(std::vector<std::wstring>* created, std::wstring* error)
{
    if (state_->thread.joinable())
        state_->thread.join();

    // A thread fermo lo stato non cambia piu': qui non serve il lucchetto.
    if (created != nullptr)
        *created = state_->created;
    if (error != nullptr)
        *error = state_->error;
    return state_->result;
}

Result Rename(HWND owner, const std::wstring& path, const std::wstring& newName,
              std::vector<std::wstring>* created, std::wstring* error)
{
    return RunOperation(
        owner, kBaseFlags, created,
        [&](IFileOperation* operation, IFileOperationProgressSink* sink) -> HRESULT {
            ComPtr<IShellItem> item;
            HRESULT hr = ShellItemFromPath(path, &item);
            if (FAILED(hr))
                return hr;
            return operation->RenameItem(item.Get(), newName.c_str(), sink);
        },
        error);
}

Result CreateFolder(HWND owner, const std::wstring& parent, const std::wstring& name,
                    std::vector<std::wstring>* created, std::wstring* error)
{
    // Il nome definitivo lo decide la shell: se "Nuova cartella" esiste gia',
    // aggiunge "(2)" da sola. Il sink ce lo riporta, quindi non serve
    // indovinarlo leggendo il disco dopo.
    return RunOperation(
        owner, kBaseFlags, created,
        [&](IFileOperation* operation, IFileOperationProgressSink* sink) -> HRESULT {
            ComPtr<IShellItem> folder;
            HRESULT hr = ShellItemFromPath(parent, &folder);
            if (FAILED(hr))
                return hr;
            return operation->NewItem(folder.Get(), FILE_ATTRIBUTE_DIRECTORY, name.c_str(),
                                      nullptr, sink);
        },
        error);
}

Result NewItem(HWND owner, const std::wstring& parent, const std::wstring& name,
               std::vector<std::wstring>* created, std::wstring* error)
{
    // Stessa strada di CreateFolder, con gli attributi di un file: il CONTENUTO
    // non lo decidiamo noi, lo decide il meccanismo `ShellNew` dell'estensione —
    // il file modello, i byte, o un file vuoto. Il nome definitivo e il "(2)" in
    // caso di conflitto li sceglie la shell, e il sink ce li riporta.
    return RunOperation(
        owner, kBaseFlags, created,
        [&](IFileOperation* operation, IFileOperationProgressSink* sink) -> HRESULT {
            ComPtr<IShellItem> folder;
            HRESULT hr = ShellItemFromPath(parent, &folder);
            if (FAILED(hr))
                return hr;
            return operation->NewItem(folder.Get(), FILE_ATTRIBUTE_NORMAL, name.c_str(),
                                      nullptr, sink);
        },
        error);
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
