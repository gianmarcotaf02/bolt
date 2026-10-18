// ---------------------------------------------------------------------------
// Gli attributi NTFS di un file, letti dal suo record nel Master File Table.
//
// Perche' non si enumerano dal file system: il file system non li espone. Quello
// che Windows sa di un file passa da `GetFileInformationByHandle` (dimensioni,
// date, flag) e da `IPropertyStore` (le proprieta' del tipo di file), ma la
// **lista degli attributi del record** — Standard Information, File Name, Data,
// Index Root, Reparse Point... — sta solo nel record del MFT, e per leggerlo
// bisogna aprire il volume raw e chiederglielo con un `FSCTL`.
//
// E' quello che fa il riferimento: gli import `CreateFileW` con la stringa
// `"\\.\"`, `DeviceIoControl` e la tabella `AT_*` (16 nomi, indicizzati per tipo
// di attributo) dicono che il programma apre il volume per **una richiesta
// mirata** e ne mostra il risultato. Non e' una scansione del volume: vedi
// `docs/01`, dove l'ipotesi "parser del MFT per le dimensioni delle cartelle" e'
// stata falsificata.
//
// Due cose vanno dette e non nascoste:
//
//   * leggere un volume raw **richiede i privilegi di amministratore**. Senza,
//     la risposta e' `NoPrivilege` e il pannello lo scrive: un elenco vuoto
//     spacciato per "questo file non ha attributi" sarebbe una bugia;
//   * il record va **corretto con la update sequence array** prima di fidarsi dei
//     suoi byte: NTFS scrive gli ultimi due byte di ogni settore in una tabella in
//     testa al record, e senza rimetterli un record puo' sembrare corrotto proprio
//     sui confini di settore.
//
// Il volume si apre in **sola lettura** (`GENERIC_READ`, nessun `FSCTL_SET_*`):
// questa funzione guarda, non tocca. L'unica cosa che scrive e' la copia del record
// in memoria, per correggerla.
//
// La parte che legge il volume e la parte che interpreta i byte sono separate
// apposta: `ParseRecord` non fa I/O, quindi si prova su un record costruito a mano
// anche su una macchina dove i privilegi non ci sono.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace NtfsAttributes
{
// Un attributo del record, come si legge.
struct Attribute
{
    uint32_t    code     = 0;      // 0x10, 0x30, 0x80...
    std::string name;              // dalla tabella `AT_*`; per gli altri, `0xNN`
    bool        resident = true;   // i byte stanno nel record, o altrove
    std::string stream;            // nome dello stream (`:stessa`), vuoto se non ne ha
    uint64_t    size     = 0;      // residente: quanti byte vale; non residente: quanti ne occupa
};

enum class Status
{
    Ok,             // letti
    NotNtfs,        // il volume non e' NTFS: la domanda non ha risposta
    NoPrivilege,    // il volume raw non si apre senza amministratore
    Unavailable,    // altro: rete, spazio dei nomi, record illeggibile
};

struct Result
{
    Status status = Status::Unavailable;
    std::vector<Attribute> attributes;
};

// Il nome del tipo, secondo la tabella `AT_*` del riferimento: gli elementi sono
// indicizzati per `(codice >> 4) - 1` — `AT_IndexRoot` e' l'elemento 8, cioe' 0x90.
// Il riferimento ne ha 16 fra 0x10 e 0x100; uno di questi (`0xD0`, EA Information)
// non e' stato visto come stringa, e quindi si scrive in esadecimale invece di
// indovinarne il nome.
std::string NameOf(uint32_t code);

// Interpreta un record del MFT gia' in memoria. Falso se non e' un record (firma
// `FILE` assente) o se la catena degli attributi esce dal record: in quel caso non
// si restituisce **meta' elenco**, perche' sembrerebbe completo.
bool ParseRecord(const uint8_t* record, size_t size, std::vector<Attribute>* out);

// Il record di un file identificato dal percorso: apre il volume, chiede il
// record, lo interpreta. Vedi `Status` per i modi in cui non riesce.
Result Read(const std::wstring& path);

} // namespace NtfsAttributes
