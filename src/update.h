// ---------------------------------------------------------------------------
// Gli aggiornamenti, nella forma B: un comando **esplicito**, niente di automatico.
//
// Il comando `Controlla aggiornamenti` e' l'unica cosa che apre una connessione:
// chiede `releases/latest` alla repo, scarica il setup dell'asset, ne verifica
// l'impronta SHA-256 pubblicata nella release, e **solo se torna** lo esegue in
// silenzioso. Una richiesta a GitHub e' telemetria di fatto (`docs/06`), quindi non
// parte da sola: parte quando qualcuno lo chiede.
//
// Qui c'e' solo il lavoro — rete, impronta, file — e sta su un **thread**: un
// download da dieci megabyte non puo' tenere ferma la finestra. Chi lo disegna e'
// `main.cpp`, che legge `Progress()` a ogni fotogramma e raccoglie l'esito con
// `Finish()` quando `Running()` diventa falso. E' la stessa forma di
// `FileOps::Job`, e per la stessa ragione.
//
// Tre rifiuti, e sono la parte che conta piu' del download:
//   * nessuna release pubblicata (GitHub risponde 404) — non e' un guasto;
//   * la release non porta `SHA256SUMS`, o non c'e' la riga del setup — senza
//     impronta non si installa niente;
//   * l'impronta non torna — il file si cancella e non parte.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace Bolt
{
namespace Update
{
// A che punto e' il lavoro. Il testo lo compone chi disegna, nella lingua scelta:
// il modulo non conosce l'interfaccia.
enum class Phase
{
    Checking,     // la richiesta a `releases/latest`
    Checksums,    // il file delle impronte
    Downloading,  // il setup
    Verifying,    // lo SHA-256 del setup scaricato
};

struct Snapshot
{
    Phase    phase    = Phase::Checking;
    uint64_t received = 0;
    uint64_t total    = 0;   // 0 quando la risposta non dichiara la lunghezza
};

enum class Result
{
    UpToDate,   // la release piu' recente non e' piu' nuova di questa build
    Ready,      // setup scaricato e verificato: il chiamante puo' eseguirlo
    Failed,     // |error| dice perche'
    Cancelled,  // richiesto con `Cancel`
};

struct ReleaseInfo
{
    std::wstring version;     // il tag, senza la `v` iniziale
    std::wstring setupPath;   // il setup scaricato e verificato
};

struct JobState;   // definito in `update.cpp`: lo condivide solo il thread

class Job
{
public:
    ~Job();

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Comincia subito: controllo, download, verifica. Torna sempre un lavoro; gli
    // errori arrivano da `Finish`, che e' l'unico posto dove si legge l'esito.
    static std::unique_ptr<Job> Start();

    bool     Running() const;
    Snapshot Progress() const;

    // Chiede di fermarsi. Non aspetta: chi l'ha chiesto lo legge in `Finish`.
    void Cancel();

    // Aspetta la fine del thread e restituisce l'esito. |release| riceve il setup
    // verificato quando il risultato e' `Ready`; |error| il motivo quando e' `Failed`.
    // Entrambi possono essere nullptr.
    Result Finish(ReleaseInfo* release, std::wstring* error);

private:
    explicit Job(std::shared_ptr<JobState> state);
    static void Run(std::shared_ptr<JobState> state);

    std::shared_ptr<JobState> state_;
    std::thread               thread_;
};

// La versione di questa build, da `CMakeLists.txt` (definita in compilazione).
std::wstring CurrentVersion();
} // namespace Update
} // namespace Bolt
