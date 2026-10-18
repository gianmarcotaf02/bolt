// ---------------------------------------------------------------------------
// Operazioni su file.
//
// Tutto passa da IFileOperation, che e' il motore che usa Esplora risorse: la
// finestra di avanzamento, il cestino, la gestione dei conflitti, la
// integrazione con Annulla e i permessi sono quelli del sistema. Reimplementarli
// a mano significherebbe rifare peggio qualcosa che Windows fa gia' bene, e
// rompersi su tutti i casi che il sistema copre da vent'anni.
//
// La clipboard usa CF_HDROP con l'effetto preferito, che e' il formato che
// Esplora risorse legge e scrive: si puo' copiare da Bolt e incollare in
// Esplora risorse, e viceversa.
//
// Le operazioni lunghe — copia, spostamento, cestino — girano su un **thread di
// lavoro**: vedi `Job` in fondo al file. Rinomina, cartella nuova e voce nuova
// restano sincrone, perche' finiscono prima che si possa disegnare un conto.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace FileOpsDetail
{
// Lo stato che il thread di lavoro scrive e il pannello legge. E' dichiarato qui
// perche' lo condivide il sink di avanzamento: la definizione sta in
// `file_ops.cpp`, ed e' privata come tutto il resto dell'esecuzione.
struct JobState;
} // namespace FileOpsDetail

namespace FileOps
{
enum class Result
{
    Ok,
    Cancelled,   // l'utente ha annullato
    Failed,      // |error| contiene il motivo
};

// Cosa fa l'operazione. Serve a dire a chi guarda cosa sta succedendo e a scegliere
// il messaggio d'esito; chi la esegue e' `Job`.
enum class Action
{
    Copy,           // copia dentro una cartella
    Move,           // sposta dentro una cartella
    Recycle,        // nel cestino
    DeleteForever,  // definitiva: non si annulla
};

// Quello che si sa dell'operazione mentre sta girando.
//
// E' una **copia**: il thread di lavoro la scrive nello stesso momento, e chi
// disegna non deve tenere un lucchetto mentre costruisce una stringa.
struct Progress
{
    std::wstring currentItem;    // la voce in corso; vuota prima che cominci
    uint64_t     totalBytes = 0;
    uint64_t     doneBytes  = 0;
    int          totalItems = 0;
    int          doneItems  = 0;
    bool         measured   = false;  // la misura dei byte e' arrivata in fondo
    bool         preparing  = true;   // si sta ancora contando: l'operazione non e' partita
    bool         cancelRequested = false;
};

// Un'operazione lunga, eseguita su un **thread di lavoro**.
//
// Prima ogni operazione girava sincrona sul thread della finestra: per una copia da
// 5 GB questo voleva dire una finestra ferma, nessun conto e nessun modo di
// fermarla. Il dialogo di progressione ha bisogno di tutte e tre le cose, e le tre
// vogliono un thread — qui c'e' il confine.
//
// Vita: si avvia con `StartTransfer` o `StartDelete`, si guarda `Running()` a ogni
// fotogramma, e quando e' falso si chiama `Finish` **una volta sola** per avere
// l'esito. `Cancel` si puo' chiamare in qualunque momento. Nessun metodo e' da
// chiamare dal thread di lavoro.
class Job
{
public:
    ~Job();

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Copia o sposta |sources| dentro |destination|. Una cartella dentro se stessa
    // non parte e l'esito lo dice `Finish`, con la stessa forma di ogni altro
    // guasto: il chiamante ha un solo posto dove leggere come e' andata.
    static std::unique_ptr<Job> StartTransfer(HWND owner, const std::vector<std::wstring>& sources,
                                              const std::wstring& destination, bool move);

    // Butta via |paths|. Con |permanently| falso va nel cestino, che e' cio' che
    // deve fare il tasto Canc: l'eliminazione definitiva e' Shift+Canc e deve
    // restare scomoda.
    static std::unique_ptr<Job> StartDelete(HWND owner, const std::vector<std::wstring>& paths,
                                            bool permanently);

    Action                   action() const;
    const std::vector<std::wstring>& sources() const;
    const std::wstring&      destination() const;

    // Vero finche' il thread di lavoro non ha finito. Non aspetta niente: e' la
    // domanda che si fa a ogni fotogramma.
    bool     Running() const;

    // Lo stato di adesso, per chi disegna.
    Progress Snapshot() const;

    // Chiede di fermarsi. Torna subito e non aspetta: l'annullamento e' una
    // richiesta, non un fatto, e chi l'ha chiesto lo vede scritto nel pannello
    // finche' la shell non ha davvero smesso.
    void     Cancel();

    // Aspetta la fine e restituisce l'esito. |created| riceve i percorsi che
    // l'operazione ha davvero prodotto — con i nomi che la shell ha scelto per i
    // conflitti — e |error| il motivo quando l'esito e' `Failed`. Entrambi possono
    // essere nullptr.
    Result   Finish(std::vector<std::wstring>* created, std::wstring* error);

private:
    explicit Job(std::shared_ptr<FileOpsDetail::JobState> state);

    // L'operazione, sul thread di lavoro. E' statica apposta: il thread riceve lo
    // stato e non il `Job`, cosi' non dipende da quanto il chiamante tiene in vita
    // l'oggetto.
    static void Run(std::shared_ptr<FileOpsDetail::JobState> state);

    std::shared_ptr<FileOpsDetail::JobState> state_;
};

// Rinomina |path| in |newName|. |newName| e' il solo nome, non un percorso.
Result Rename(HWND owner, const std::wstring& path, const std::wstring& newName,
              std::vector<std::wstring>* created, std::wstring* error);

// Crea una cartella e ne restituisce il percorso in |created|.
Result CreateFolder(HWND owner, const std::wstring& parent, const std::wstring& name,
                    std::vector<std::wstring>* created, std::wstring* error);

// Crea una voce nuova col meccanismo del sistema: `IFileOperation::NewItem`
// applica i dati `ShellNew` dell'estensione — il file modello, i byte, o niente —
// quindi "Nuovo documento di testo.txt" nasce come lo fa Esplora risorse. Il nome
// definitivo lo sceglie la shell, come per la cartella.
Result NewItem(HWND owner, const std::wstring& parent, const std::wstring& name,
               std::vector<std::wstring>* created, std::wstring* error);

// --- clipboard -------------------------------------------------------------

// Mette |paths| nella clipboard. Con |cut| l'effetto preferito e' lo spostamento,
// ed e' quello che fa disegnare le icone in grigio a Esplora risorse.
bool SetClipboardFiles(HWND owner, const std::vector<std::wstring>& paths, bool cut);

// Vero se la clipboard contiene un elenco di file.
bool ClipboardHasFiles();

// Legge l'elenco di file dalla clipboard. |cut| riceve l'effetto preferito.
bool ReadClipboardFiles(std::vector<std::wstring>* paths, bool* cut);

// Svuota la clipboard, dopo che un incolla l'ha consumata.
void ClearClipboard(HWND owner);
} // namespace FileOps
