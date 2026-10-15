// ---------------------------------------------------------------------------
// Sorvegliare una cartella.
//
// `ReadDirectoryChangesW` e' l'unico modo che Windows da' di sapere che qualcosa
// e' cambiato senza chiederlo: senza, l'unica strada e' rileggere (F5). Il
// riferimento la usa per invalidare la voce corrispondente (`docs/01`,
// `docs/02`), ed e' il pezzo che chiude M3.
//
// Tre decisioni, e le ragioni.
//
//  * Si sorveglia **una** cartella: quella aperta. La sorveglianza di Windows non
//    e' ricorsiva, e registrare un handle per ogni cartella di un albero ne
//    aprirebbe migliaia. Un cambiamento **dentro una sottocartella** si vede lo
//    stesso, per un'altra via: modificare il contenuto di una cartella ne cambia
//    la data di modifica, e quella e' una modifica di una voce **diretta** della
//    cartella sorvegliata, quindi arriva. Un cambiamento piu' in fondo di cosi'
//    resta invisibile: limite dichiarato, e scritto in `docs/04`.
//
//  * Un thread solo, in attesa con I/O sovrapposto: non fa niente finche' non
//    succede qualcosa, e la lettura si annulla quando si cambia cartella. Un giro
//    di attesa a intervalli sarebbe lavoro sprecato **e** in ritardo.
//
//  * La finestra non riceve un messaggio per ogni file toccato: il thread
//    raccoglie i cambiamenti e manda **un** messaggio; chi lo riceve prende tutto
//    quello che c'e'. Copiare diecimila file non deve produrre diecimila
//    ricaricamenti.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class DirectoryWatcher
{
public:
    struct Change
    {
        std::wstring path;         // percorso completo della voce cambiata
        DWORD        action = 0;   // FILE_ACTION_*, come li da' Windows
    };

    // Vero quando l'azione cambia **l'elenco** delle voci — una voce creata,
    // cancellata, rinominata — e non solo il contenuto o la data di una che c'e'
    // gia'. E' questa la domanda che decide se ricaricare le righe, e sta qui
    // perche' la fanno in due: chi raccoglie le notifiche e chi le applica.
    static bool ChangesListing(DWORD action)
    {
        return action == FILE_ACTION_ADDED || action == FILE_ACTION_REMOVED ||
               action == FILE_ACTION_RENAMED_OLD_NAME || action == FILE_ACTION_RENAMED_NEW_NAME;
    }

    DirectoryWatcher() = default;
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    // Avvia il thread. |window| riceve |message| quando qualcosa cambia; il
    // messaggio non dice cosa, perche' chi lo riceve prende i cambiamenti con
    // `Take`. Falso se il thread non parte.
    bool Start(HWND window, UINT message);
    void Stop();

    // Cambia la cartella sorvegliata. Si puo' chiamare a ogni navigazione: se e'
    // la stessa non fa niente.
    void Watch(const std::wstring& directory);

    // I cambiamenti accumulati da quando li si e' presi l'ultima volta, senza
    // doppioni sullo stesso percorso (vince l'ultima azione). Falso se non c'e'
    // niente. Da chiamare sul thread della finestra.
    bool Take(std::vector<Change>* changed);

private:
    void ThreadMain();
    void Collect(const BYTE* buffer);
    void Report();

    HWND window_ = nullptr;
    UINT message_ = 0;

    std::thread thread_;
    HANDLE stopEvent_   = nullptr;
    HANDLE ioEvent_     = nullptr;
    HANDLE targetEvent_ = nullptr;

    std::vector<BYTE> buffer_;

    std::mutex mutex_;
    std::wstring target_;                              // prossima cartella da sorvegliare
    std::unordered_map<std::wstring, DWORD> changed_;  // percorso -> ultima azione
    bool notified_ = false;                            // un messaggio e' gia' in coda

    bool running_ = false;
};
