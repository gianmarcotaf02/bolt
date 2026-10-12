// ---------------------------------------------------------------------------
// Le destinazioni di "Invia a".
//
// La shell il proprio "Invia a" lo popola in un sottomenu, e i comandi di quel
// sottomenu non si possono eseguire da fuori (`docs/02` §7.19): il menu lo
// costruisce Bolt, dalla stessa fonte che usa la shell — e qui la fonte non e'
// nascosta come le chiavi `ShellNew`, perche' **"Invia a" e' una cartella vera**:
//
//     %APPDATA%\Microsoft\Windows\SendTo
//
// con dentro il collegamento a ogni destinazione. Chi vuole una voce in piu' la
// trascina li' dentro, e questo menu la mostra al controllo successivo — per
// questo l'elenco NON si mette in cache, al contrario di quello del "Nuovo":
// costa una `FindFirstFile` su una manciata di file, e la cartella puo' cambiare
// mentre il programma gira.
//
// L'invocazione non sta qui: la fa chi ha i percorsi scelti (`SendToTargets`).
// Una destinazione si esegue come la esegue la shell — `ShellExecuteEx` sul
// collegamento, con i percorsi scelti come argomenti — e non c'e' niente di
// nostro da aggiungere.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace ShellSendTo
{
struct Target
{
    std::wstring label;  // il nome del file senza estensione: "Cartella compressa"
    std::wstring path;   // il collegamento (o l'eseguibile) da eseguire
};

// Le voci della cartella SendTo dell'utente, in ordine di etichetta. Vuoto se la
// cartella non c'e' o e' vuota: allora la voce "Invia a" non si offre, perche' un
// sottomenu vuoto non si spiega.
//
// Ritorna una copia, e la legge ogni volta: e' una cartella dell'utente, e chi ci
// mette dentro un collegamento nuovo si aspetta di vederlo (vedi la testa).
std::vector<Target> Targets();
} // namespace ShellSendTo
