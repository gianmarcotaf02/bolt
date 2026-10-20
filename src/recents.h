// ---------------------------------------------------------------------------
// Il ranking dei recenti (M4).
//
// Il riferimento tiene, per ogni percorso visitato, tre numeri che scrive nella
// sessione: `Score`, `Decay`, `Ordinal`. Questa e' la formula ricostruita da REA
// il 2026-10-18 — `FUN_140189bb0` aggiorna, `FUN_140188c20` crea la voce,
// `FUN_1401a9c50` la formatta — e la forma dei numeri e' quella del binario:
// `Score` e `Decay` a 16 bit, `Ordinal` a 32.
//
//   visita di un percorso
//     Score   = min(10, Score + peso)      peso 3 per la navigazione
//     Decay   = 0
//     Ordinal = (il piu' grande Ordinal di tutte le voci) + 1
//
//   ogni altra voce nella *stessa cartella* (stesso genitore)
//     Decay   = Decay + 1                  fino a 10
//     a Decay gia' a 10: Score = max(Score - 1, 0)
//
// Cioe': il punteggio e' quanto spesso quella cartella si e' aperta, saturato a
// 10; `Decay` conta quante volte si e' passati a una cartella *sorella* senza
// tornare; passate le dieci, il punteggio scende di uno a ogni passaggio. E'
// l'opacita' con cui la lista evidenzia i recenti: `Score / 10` fra 0,25 e 1,0
// (0x140128780).
//
// L'ordine di presentazione e' `Score` decrescente e, a pari punteggio,
// `Ordinal` decrescente. `Ordinal` esiste per questo: con `Score` saturo a 10
// l'ordine dei piu' usati non si distingue piu' — e' l'unico dei tre campi che
// sa ancora dire quale e' stato aperto per ultimo.
//
// Nessuno dei tre modi scrive su disco: `docs/06` lo vieta, e il modo dice solo
// **cosa si registra**, non dove finisce. La preferenza non si salva: vale per
// la sessione, come la mappa dei tasti.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Bolt
{
// I tre valori di `StoreRecents` nella tabella dello spec (`docs/03`).
//
//   Session     tutte le cartelle, per questa sessione (ed e' la scelta, docs/06)
//   Persistent  tutte le cartelle, pensate per sopravvivere alla sessione nel
//               riferimento (`"Recents": [` nella sessione): qui restano in
//               memoria, e il limite e' dichiarato in `docs/04` M4
//   LocalDisk   solo i volumi locali fissi
//
// `LocalDisk` non e' un sinonimo: la voce dei recenti porta con se' `DriveFixed`
// e `DriveSerial` proprio per questo, e nominarla "disco locale" e poi registrare
// anche un percorso di rete sarebbe un controllo che dichiara il falso.
enum class StoreRecents
{
    Session,
    Persistent,
    LocalDisk,
};

// Una voce: il percorso, i tre campi del punteggio, e l'identita' del volume che
// il riferimento conserva accanto ai tre (`DriveFixed`, `DriveSerial`, `UncRoot`)
// per non confondere due percorsi uguali su volumi diversi.
struct RecentEntry
{
    std::wstring path;
    std::wstring uncRoot;      // per un percorso di rete: `\\server\share`
    bool         isDirectory = true;
    bool         driveFixed  = false;
    uint32_t     driveSerial = 0;
    uint16_t     score       = 0;   // 0..10, satura
    uint16_t     decay       = 0;   // 0..10, azzerato dalla visita
    uint32_t     ordinal     = 0;   // contatore delle visite, cresce sempre
};

class Recents
{
public:
    static constexpr uint16_t kMaxScore = 10;
    static constexpr uint16_t kMaxDecay = 10;
    // Il peso di una navigazione: `FUN_140002de0`, `FUN_14007d6d0` e
    // `FUN_1400804b0` passano 3; `FUN_14007fa10` passa 1. Tre visite saturano il
    // punteggio, che e' il senso di un valore piccolo con un tetto a 10.
    static constexpr int    kNavWeight = 3;
    static constexpr size_t kMaxEntries = 20;

    // Registra una visita. E' l'unica porta: deduplica, aggiorna i tre campi e fa
    // decadere i fratelli. `isDirectory` distingue una cartella aperta da un file
    // aperto: la barra laterale mostra solo cartelle, il GoTo anche i file.
    void Touch(const std::wstring& path, int weight = kNavWeight, bool isDirectory = true);

    // Le voci nell'ordine in cui si mostrano: `Score` decrescente, poi `Ordinal`.
    std::vector<RecentEntry> Ranked() const;

    // `Cancella recenti`, e la singola voce che sparisce (`Rimuovi dai recenti`).
    void Clear();
    void Forget(const std::wstring& path);

    StoreRecents StoreMode() const { return storeMode; }
    void SetStoreMode(StoreRecents mode) { storeMode = mode; }   // non si salva: docs/06

    // Il punteggio di un percorso, o `-1` se non e' un recente. E' la domanda
    // che fa la lista per evidenziarlo: le voci sono al massimo venti, quindi si
    // scorre l'elenco invece di tenere un secondo indice da tenere allineato.
    int ScoreFor(const std::wstring& path) const;

    // I nomi (ultimo pezzo del percorso, in minuscolo) dei recenti che stanno
    // **dentro** una cartella. Serve a `SortRecents`: la lista li mette in cima, e
    // la domanda si fa una volta per ordinamento, non una per confronto.
    std::vector<std::wstring> NamesIn(const std::wstring& directory) const;

private:
    RecentEntry*       FindMutable(const std::wstring& path);
    static bool        SamePath(const std::wstring& a, const std::wstring& b);
    static bool        SameParent(const std::wstring& a, const std::wstring& b);

    std::vector<RecentEntry> entries;
    uint32_t                 ordinalCounter = 0;
    StoreRecents             storeMode      = StoreRecents::Session;
};

// L'opacita' con cui il riferimento evidenzia un recente nella lista: 0,25 a
// punteggio 0, 1,0 a punteggio 10 (`0x140128780`). Sta qui e non nel disegno
// perche' e' la **scala dei campi**, non una scelta di grafica: se domani il
// punteggio massimo cambia, cambia in un posto solo.
float RecentOpacity(int score);

// Un nome confrontabile: le due stringhe che descrivono lo stesso file possono
// avere la maiuscola scritta in modo diverso a seconda di come ci si e' arrivati.
std::wstring FoldCase(const std::wstring& text);
}   // namespace Bolt
