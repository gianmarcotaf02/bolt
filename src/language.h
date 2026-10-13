// ---------------------------------------------------------------------------
// La lingua dell'interfaccia.
//
// Le due lingue di una stringa stanno **dove si usa la stringa**, non in un file
// di traduzioni: si legge cosa diventa senza doverlo cercare altrove, e aggiungere
// una stringa non e' un'operazione in due posti. Qui sta solo **quale** delle due
// si mostra, perche' la scelta vale per tutta l'applicazione e non per un
// pannello. Vedi AGENTS.md §3.
//
// Vive fuori da `main.cpp` perche' anche `file_ops.cpp` ha due o tre messaggi
// d'errore suoi: il secondo utilizzatore e' la ragione per cui e' un modulo.
// ---------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <string>

enum class Language
{
    Italian,
    English,
};

// La lingua corrente. La cambiano la palette e il pannello delle opzioni, e la
// rilegge `%APPDATA%\Bolt\settings.ini` all'avvio.
extern Language gLanguage;

const char*  Tr(const char* italian, const char* english);
std::wstring TrW(const wchar_t* italian, const wchar_t* english);

// "3 elementi" / "1 elemento", col verbo giusto: "Copiati 3 elementi" ma "Copiato
// 1 elemento". L'inglese non cambia il verbo, quindi |en| e' uno solo.
std::wstring CountItems(std::size_t count);
std::wstring VerbItems(std::size_t count, const wchar_t* oneIt, const wchar_t* manyIt,
                       const wchar_t* en);
