#include "dir_watch.h"

#include "diag.h"

#include <algorithm>

namespace
{
// Quello che si vuole sapere: i nomi (una voce creata, cancellata, rinominata), le
// dimensioni e la data di modifica (una voce cambiata), e nient'altro. Chiedere
// anche gli attributi o la sicurezza vorrebbe dire svegliare il thread per cose
// che non si mostrano.
constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                          FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                          FILE_NOTIFY_CHANGE_CREATION;

// Sessantaquattro kilobyte di notifiche in un colpo: un buffer piccolo si riempie
// e Windows risponde "non so dirti tutto" (ERROR_NOTIFY_ENUM_DIR), che qui
// significherebbe invalidare l'intera cartella per non sapere cosa e' cambiato.
constexpr DWORD kBufferBytes = 64 * 1024;
} // namespace

DirectoryWatcher::~DirectoryWatcher()
{
    Stop();
}

bool DirectoryWatcher::Start(HWND window, UINT message)
{
    if (running_)
        return true;

    window_  = window;
    message_ = message;

    stopEvent_   = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ioEvent_     = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    targetEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stopEvent_ == nullptr || ioEvent_ == nullptr || targetEvent_ == nullptr)
        return false;

    buffer_.assign(kBufferBytes, 0);

    running_ = true;
    thread_  = std::thread(&DirectoryWatcher::ThreadMain, this);
    return true;
}

void DirectoryWatcher::Stop()
{
    if (!running_)
        return;

    SetEvent(stopEvent_);
    if (thread_.joinable())
        thread_.join();
    running_ = false;

    for (HANDLE* event : {&stopEvent_, &ioEvent_, &targetEvent_})
    {
        if (*event != nullptr)
        {
            CloseHandle(*event);
            *event = nullptr;
        }
    }
}

void DirectoryWatcher::Watch(const std::wstring& directory)
{
    if (!running_)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (target_ == directory)
            return;
        target_ = directory;
    }
    // Il thread legge sempre il valore **piu' recente**: se due navigazioni
    // arrivano in fretta, la seconda non si perde, perche' non e' l'evento a
    // portare il dato.
    SetEvent(targetEvent_);
}

void DirectoryWatcher::Report()
{
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!changed_.empty() && !notified_)
        {
            notified_ = true;
            notify    = true;
        }
    }
    if (notify)
        PostMessageW(window_, message_, 0, 0);
}

bool DirectoryWatcher::Take(std::vector<Change>* changed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    notified_ = false;
    if (changed_.empty())
        return false;

    changed->reserve(changed_.size());
    for (const auto& item : changed_)
        changed->push_back(Change{item.first, item.second});
    changed_.clear();
    return true;
}

void DirectoryWatcher::Collect(const BYTE* buffer)
{
    std::wstring base;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        base = target_;
    }
    if (base.empty())
        return;

    DWORD offset = 0;
    int records = 0;
    int added = 0, removed = 0, modified = 0, renamed = 0;
    for (;;)
    {
        const auto* record = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer + offset);
        const std::wstring name(record->FileName, record->FileNameLength / sizeof(WCHAR));
        ++records;
        switch (record->Action)
        {
            case FILE_ACTION_ADDED:    ++added;    break;
            case FILE_ACTION_REMOVED:  ++removed;  break;
            case FILE_ACTION_MODIFIED: ++modified; break;
            default:                   ++renamed;  break;
        }

        std::wstring path = base;
        if (!path.empty() && path.back() != L'\\')
            path += L'\\';
        path += name;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Un percorso toccato due volte e' una voce sola, ma **l'azione che
            // cambia l'elenco vince**: scrivere un file lo crea e poi lo modifica,
            // e sono due notifiche sullo stesso percorso. Tenendo l'ultima azione
            // la creazione sparirebbe dietro la modifica, e la riga non comparirebbe
            // — che e' esattamente quello che e' successo la prima volta.
            const auto previous = changed_.find(path);
            const bool alreadyMatters =
                previous != changed_.end() && ChangesListing(previous->second);
            if (!alreadyMatters)
                changed_[path] = record->Action;
        }

        if (record->NextEntryOffset == 0)
            break;
        offset += record->NextEntryOffset;
    }

    if (DiagEnabled())
    {
        // Si contano, non si trascrivono: il nome di una voce cambiata e' roba di
        // chi lavora, e nel registro non ci va (`AGENTS.md` §0).
        std::lock_guard<std::mutex> lock(mutex_);
        DiagLog("--- " + DiagClock() + " sorveglianza: " + std::to_string(records) +
                " notifiche (create " + std::to_string(added) + ", cancellate " +
                std::to_string(removed) + ", modificate " + std::to_string(modified) +
                ", rinominate " + std::to_string(renamed) + ")");
    }

    Report();
}

void DirectoryWatcher::ThreadMain()
{
    for (;;)
    {
        // In attesa di una cartella da sorvegliare, o dello stop.
        HANDLE idle[2] = {stopEvent_, targetEvent_};
        const DWORD woken = WaitForMultipleObjects(2, idle, FALSE, INFINITE);
        if (woken == WAIT_OBJECT_0)
            return;   // stop
        if (woken != WAIT_OBJECT_0 + 1)
            continue;
        ResetEvent(targetEvent_);

        std::wstring directory;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            directory = target_;
        }
        if (directory.empty())
            continue;

        // Un handle per la cartella, sovrapposto: e' quello che permette di
        // annullare la lettura quando si cambia cartella, invece di aspettare che
        // succeda qualcosa per accorgersi che non serve piu'.
        HANDLE handle = CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            DiagLog("--- " + DiagClock() + " sorveglianza: la cartella non si apre, si aspetta la prossima");
            continue;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = ioEvent_;

        for (;;)
        {
            ResetEvent(ioEvent_);
            const BOOL issued = ReadDirectoryChangesW(handle, buffer_.data(), kBufferBytes,
                                                     FALSE, kFilter, nullptr, &overlapped, nullptr);
            if (!issued)
            {
                DiagLog("--- " + DiagClock() + " sorveglianza: la lettura non parte (errore " +
                        std::to_string(GetLastError()) + ")");
                break;
            }

            HANDLE waits[3] = {stopEvent_, ioEvent_, targetEvent_};
            const DWORD got = WaitForMultipleObjects(3, waits, FALSE, INFINITE);

            if (got == WAIT_OBJECT_0)
            {
                // Stop: si annulla la lettura in corso, o la cartella resta aperta
                // finche' non arriva qualcosa che non arrivera' mai.
                CancelIoEx(handle, &overlapped);
                CloseHandle(handle);
                return;
            }
            if (got == WAIT_OBJECT_0 + 2)
            {
                // Un'altra cartella: si smette di leggere questa.
                ResetEvent(targetEvent_);
                CancelIoEx(handle, &overlapped);
                break;
            }

            DWORD bytes = 0;
            if (!GetOverlappedResult(handle, &overlapped, &bytes, FALSE))
            {
                const DWORD error = GetLastError();
                if (error == ERROR_NOTIFY_ENUM_DIR)
                {
                    // Il buffer si e' riempito e Windows non sa dire cosa e'
                    // cambiato: si invalida la cartella intera, che e' l'unica cosa
                    // che si puo' dire senza inventare.
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!target_.empty())
                        changed_[target_] = 0;
                    Report();
                    continue;
                }
                break;   // cartella sparita, o lettura annullata
            }
            if (bytes > 0)
                Collect(buffer_.data());
        }

        CloseHandle(handle);
    }
}
