// ---------------------------------------------------------------------------
// I supporti che piu' di un modulo usa: testo, formattazione, nomi di percorso.
//
// Sono le funzioni che servivano a tutti e non appartenevano a nessuno: stavano in
// `main.cpp` dentro un namespace anonimo, quindi nessun altro file poteva
// chiamarle, e per spostare una vista bisognava prima spostare queste.
//
// Qui non c'e' niente che riguardi il *disegno*: `FitText` misura e taglia un
// testo, non lo disegna, e le altre scrivono numeri e date. Chi disegna include
// questo file, non il contrario.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>

#include "app_state.h"

namespace Bolt
{
// Il tempo in secondi, monotono: serve a misurare intervalli (il sovrappasso, i
// messaggi di stato) e non a sapere che ora e'.
double NowSeconds();

// Quanto e' "comparso" un pannello aperto a |openedAt|: 0 appena aperto, 1 quando
// l'animazione e' finita. Con `Animations` spenta torna sempre 1 — il pannello c'e'
// e basta, che e' il comportamento di prima. L'ultima riga e' quella che serve a chi
// disegna: se e' < 1 il fotogramma dopo va chiesto di nuovo, o l'animazione si ferma
// al primo disegno.
float PanelAppear(double openedAt, bool animations);

// Converte fra UTF-8 e UTF-16. In tutto il programma le stringhe di interfaccia
// sono strette (UTF-8) e quelle del sistema sono larghe; la conversione sta qui
// perche' e' l'unica cosa che le mette in comunicazione.
std::string  ToUtf8(const std::wstring& text);
std::wstring Widen(const std::string& text);

// Un nome di percorso: l'ultimo pezzo, e il pezzo che lo contiene.
std::wstring LeafName(const std::wstring& path);
std::wstring ParentOf(const std::wstring& path);

// Il percorso nella forma che Win32 accetta quando supera i 260 caratteri: il
// prefisso `\\?\`, o `\\?\UNC\` per un percorso di rete. Con `longPaths` spento —
// il valore di partenza — torna quello che e' entrato, quindi chi chiama non deve
// sapere niente: `IOPath(path, app.longPaths)`.
//
// Non lo vogliono la shell (`IFileOperation`, `ShellExecute`, i menu contestuali)
// ne' i percorsi relativi o gia' prefissati: li' il prefisso cambia il significato
// del nome. La funzione si usa **nei punti di I/O**, non su ogni stringa di
// percorso.
std::wstring IOPath(const std::wstring& path, bool longPaths);
std::wstring JoinPath(const std::wstring& directory, const std::wstring& name);

// Come si scrive una grandezza a chi la legge: "1,2 MB", "13,9K". Rotte le unita'
// si perdono i dettagli che servono; intere si perde la misura.
std::string HumanBytes(uint64_t bytes);
std::string HumanCount(uint64_t value);

// Una data del file system. `withTime` distingue la colonna dell'elenco (compatta)
// dall'inspector (con l'ora), e un tempo a zero non e' il 1601: e' "non lo so".
std::string FormatFileTime(const FILETIME& fileTime, bool withTime);
uint64_t    FileTimeToUint64(const FILETIME& fileTime);

// La linguetta che apre e chiude una sezione: un triangolo disegnato, non il
// carattere `>` o `v`. La **forma** si decide qui e non in chi la disegna, perche'
// la linguetta compare in quattro posti — barra laterale, gruppi della lista,
// palette, GoTo — e quattro disegni della stessa cosa sono quattro occasioni di
// farne uno diverso.
//
// Non e' equilatera: e' **piu' larga che profonda**, cosi' non sembra una punta.
// E' la misura che si vede, non una proporzione: il triangolo si iscrive nel
// rettangolo che riceve, e qui dentro il rettangolo si sceglie per il verso.
UIDraw DisclosureArrow(const Rect& box, Color color, bool pointsDown);

// Il centro verticale di un testo dentro una riga: la misura dipende dal carattere
// e dalla scala, quindi si calcola invece di fissarla. Lo usano tutti i pannelli.
float TextY(const Layout& layout, const Rect& row, const FontAtlas& font);

// Taglia un testo alla larghezza che ha, con l'ellissi. `FitTextTail` taglia
// **davanti** invece che in fondo: di un percorso la parte che dice dove si va e'
// la coda.
std::string FitText(const FontAtlas& font, const std::string& text, float scale, float maxWidth);
std::string FitTextTail(const FontAtlas& font, const std::string& text, float scale,
                        float maxWidth);

// Il nome del tipo come lo da' la shell ("Cartella di file", "Excel.CSV"), che e'
// anche l'unico modo di scriverlo tradotto come lo scrive Windows.
std::string ShellTypeName(const std::string& extension, bool isDirectory);

// Come si chiama una posizione: il suo nome se e' un percorso, il nome che le da'
// la shell se e' una cartella dello spazio dei nomi — "Cestino", non
// "::{645FF040-...}".
std::wstring LocationLabel(const AppState& app, const std::wstring& location);

// Il nome **corto** di un percorso, come lo chiama Windows: "Documenti", non
// "Documents". Per una cartella normale e' la coda del percorso; per una cartella
// nota e' il nome tradotto dalla shell. Si ricorda, perche' la barra laterale lo
// chiede a ogni fotogramma e una domanda alla shell per riga sarebbe un costo che
// non si vede ma si paga.
std::wstring FriendlyName(const std::wstring& path);

// L'atlante dei glifi si rifa' solo quando una delle tre misure del carattere cambia
// davvero: rasterizzarlo a ogni fotogramma sarebbe un lavoro che nessuno vede.
void ApplyFonts(AppState& app);
} // namespace Bolt

