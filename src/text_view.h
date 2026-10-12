// ---------------------------------------------------------------------------
// Il testo di un file, per il visualizzatore dell'inspector.
//
// Un file di testo non e' una cosa sola: puo' essere UTF-8 con o senza marcatore
// d'ordine, UTF-16 nei due versi, o "ANSI" — cioe' la codifica locale, che su una
// macchina italiana e' latin-1 e su altre e' un'altra cosa. E puo' non essere testo
// affatto, che e' il caso piu' importante da riconoscere: aprire un eseguibile come
// se fosse una pagina di prosa riempirebbe la finestra di rumore, e sembrerebbe un
// difetto nostro.
//
// Quindi: si legge, si **decide** se e' testo guardando il contenuto (non
// l'estensione: un .txt puo' essere binary e un .dat puo' essere testo), si
// decodifica in UTF-8 — la forma che il disegno sa gia' usare tutta — e si
// spezza in righe, tenendo conto che i terminatori di riga sono tre e convivono.
//
// **Niente cache su disco** (`docs/06`): il file si legge quando si guarda e si
// tiene in memoria finche' serve. E niente file interi: oltre un megabyte si legge
// l'inizio e lo si dichiara, perche' una lettura sincrona di cento megabyte
// bloccherebbe la finestra per un tempo che l'utente non ha chiesto.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace TextView
{
enum class Encoding
{
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Ansi,      // CP_ACP: sulla macchina locale, latin-1
    Binary,    // non e' testo: non si mostra
};

struct Document
{
    bool        isText = false;
    Encoding    encoding = Encoding::Utf8;
    uint64_t    byteSize = 0;
    bool        truncated = false;   // si e' letto solo l'inizio

    std::vector<std::string> lines;  // senza terminatori, in UTF-8

    // I terminatori trovati: "CRLF", "LF", "CR", o "misto". Serve a dirlo: un file
    // con due convenzioni dentro si vede subito quando lo si apre con un editor
    // vero, e tacerlo sarebbe un'altra cosa che l'utente scopre dopo.
    std::string endings;
};

// Come si chiama la codifica, per la riga di stato del visualizzatore.
const char* EncodingName(Encoding encoding);

// Legge e decodifica. Un file che non si apre resta `isText == false`.
Document Load(const std::wstring& path);
} // namespace TextView
