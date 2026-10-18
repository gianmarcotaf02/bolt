// ---------------------------------------------------------------------------
// Le impostazioni, viste da fuori.
//
// Quattro nomi: dove sta il file, rileggerlo, scriverlo, e ricordarlo (che vuol dire
// scrivere e, se non si riesce, dirlo invece di far finta). Il formato e' una riga
// per preferenza in `%APPDATA%\Bolt\settings.ini`: testo semplice, cancellabile a
// mano, nessun binario — vedi `docs/06`.
// ---------------------------------------------------------------------------
#pragma once

#include <string>

#include "app_state.h"

namespace Bolt
{
// Il percorso del file: `%APPDATA%\Bolt\settings.ini`. Vuoto se il sistema non dice
// dov'e' `AppData` — e allora non si salva niente, invece di inventare un posto.
std::wstring SettingsPath();

// Legge le preferenze e le applica: lingua, caratteri, tavolozza, opzioni della
// lista. Un file assente non e' un errore: sono i valori di partenza.
void LoadSettings(AppState& app);

// Scrive il file. Falso se non ci e' riuscito — e chi lo chiama lo dice.
bool SaveSettings(const AppState& app);

// Ricorda la preferenza e, se non si riesce, lo dice: e' l'unico modo di sapere che
// una scelta non verra' ritrovata al prossimo avvio.
void RememberSettings(AppState& app);
} // namespace Bolt
