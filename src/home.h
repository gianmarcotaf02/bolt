// ---------------------------------------------------------------------------
// La Home: la pagina di avvio.
//
// **Non e' una cartella finta**: e' una posizione come "Questo PC", con un nome
// suo (`bolt:home`) e delle voci che il file system non ha. Una cartella vera
// costringerebbe a crearne una sul disco e a riempirla; qui invece ogni riga e'
// una destinazione gia' esistente — un fissaggio, un recente, una cartella nota —
// e `Entry::location` porta il percorso vero, che e' il campo che la lista usa gia'
// per le voci del cestino.
//
// Tre bande, in ordine di importanza: **Fissaggi**, **Recenti**, **Cartelle note**.
// Una destinazione che starebbe in due bande compare una volta sola, nella prima:
// due righe per la stessa cartella sarebbero due nomi per la stessa cosa.
//
// Quello che la Home **non** e': una cartella su cui si opera. Copia, elimina,
// rinomina e proprieta' si rifiutano, perche' la riga nomina una cartella vera e
// senza quella guardia `Elimina` cancellerebbe quella. La sorveglianza della
// cartella e le statistiche ricorsive non partono: non c'e' una cartella da
// sorvegliare e non c'e' niente da contare.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// Il nome della posizione. Non comincia per `::` ne' per `shell:`, quindi non
// viene scambiato per un nome dello spazio dei nomi della shell: e' nostro.
inline constexpr const wchar_t* kHomeLocation = L"bolt:home";

bool IsHome(const std::wstring& location);

// Le voci delle tre bande, gia' nell'ordine in cui si mostrano. Ogni voce ha
// `location` pieno — il campo dice dove si va davvero — e `band` a 0, 1 o 2.
std::vector<Entry> BuildHomeEntries(const AppState& app);

// Il titolo di una banda. La chiave e' il numero, non il nome: una banda chiusa
// deve restare chiusa anche cambiando lingua.
const char* HomeBandLabel(int band);
} // namespace Bolt
