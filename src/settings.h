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

// La **sessione**: le schede aperte. E' l'unica cosa che questo programma scrive su
// disco che non e' una preferenza — per riaprire le schede bisogna pur ricordarsi
// quali erano. Per questo sta in un file **suo**, `session.ini`, creato solo se
// `rememberTabs` e' acceso; spegnendo l'interruttore il file viene **cancellato**
// invece di restare li'.
std::wstring SessionPath();
void SaveSession(const AppState& app);
// Vero se ha riaperto almeno una scheda. Quelle che non esistono piu' si saltano.
bool RestoreSession(AppState& app);
} // namespace Bolt
