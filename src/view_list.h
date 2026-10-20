// ---------------------------------------------------------------------------
// La lista: le righe, le colonne, la griglia, i gruppi, il filtro.
//
// E' il terzo pezzo di **vista** uscito da `main.cpp`, e il piu' grosso: ordinamento,
// filtro, gruppi e viste passano tutti di qui, perche' tutti finiscono nelle stesse
// righe. Il conto e' quello di `RebuildView` — dalla cartella alle righe che si
// vedono — e il disegno e' `DrawList`, che le righe le riceve gia' fatte.
//
// La separazione e' quella degli altri moduli: cos'e' una riga (un'intestazione di
// gruppo, o una voce), dove sta (la geometria) e come si disegna sono tre domande
// diverse. `ComputeViewMetrics` e `ItemRect` rispondono alla seconda, e le usa anche
// l'input per capire dove si e' cliccato: due calcoli della stessa geometria
// divergono sempre, e qui il secondo sarebbe stato invisibile.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// --- modello: dall'elenco delle voci alle righe che si vedono ----------------
//
// L'ordinamento dispone le voci, il filtro ne toglie, il raggruppamento infila
// intestazioni fra loro: quello che si disegna non e' la cartella ma una lista di
// righe, e si rifa' solo quando cambia qualcosa da cui dipende.
bool SortEntries(AppState& app);
void RebuildView(AppState& app);

// Il valore di una colonna per una voce, **come si vede**: lo usano il filtro per
// valore e il pannello dei valori di un'intestazione, cosi' il filtro parla la stessa
// lingua della colonna che filtra.
std::string ColumnValueOf(const AppState& app, const Entry& entry, int column);

// Il nome come si **scrive**: con `ShowFileExtension` spento, senza estensione.
// Il nome vero resta quello della voce, e questa e' l'unica differenza fra i due.
std::string DisplayName(const AppState& app, const Entry& entry);

// --- colonne e viste ---------------------------------------------------------
void InitColumns(AppState& app);
void SetViewMode(AppState& app, ViewMode mode);

// I **layout per cartella** (`DirHostLayoutSave`/`DirHostLayoutDel`, M9), in memoria.
// Riflettono `AppState` in un valore, e quel valore lo rimettono: sono le due
// direzioni della stessa cosa, come `RefreshOptionItem`/`ApplyOptionFromItem`.
void SaveFolderLayout(AppState& app);      // la cartella aperta ricorda com'e' guardata
void ForgetFolderLayout(AppState& app);    // e se ne dimentica
bool ApplyFolderLayout(AppState& app);     // vero se quella cartella ne aveva uno

// --- geometria ---------------------------------------------------------------
//
// La misura della vista, l'altezza del contenuto e il rettangolo di una riga: le
// stesse risposte servono a disegnare, a scorrere e a sapere dove si e' cliccato.
// La geometria della griglia si chiede con `ItemRect`, che la calcola e la tiene —
// una banda di gruppo, li' dentro, spezza il flusso e l'indice da solo non basta.
ViewMetrics ComputeViewMetrics(const AppState& app, const Layout& layout);
float       ViewContentHeight(AppState& app, const Layout& layout);
Rect        ItemRect(AppState& app, const Layout& layout, int rowIndex, float scroll);

// --- disegno -----------------------------------------------------------------
void DrawHeader(AppState& app, const Layout& layout, float* columnX);
void DrawFilterBar(AppState& app, const Layout& layout);
void DrawList(AppState& app, const Layout& layout, const float* columnX);

// --- le voci: domande che piu' di un pezzo del programma fa -------------------
//
// Il cursore, la selezione, il percorso di una voce e la sua icona: le fa la lista
// mentre disegna e le fa l'input quando riceve un clic, e devono rispondere allo
// stesso modo. Stavano in `main.cpp` e le usavano in tre.
int  FindEntryByName(const AppState& app, const std::wstring& name);
int  CursorIndex(const AppState& app);
bool IsEntrySelected(const AppState& app, const Entry& entry);

// Cio' che e' selezionato **e visibile**, nell'ordine della lista: i percorsi completi
// e i PIDL per le voci che ne hanno uno. Il filtro fa parte della domanda — filtrare
// puo' nascondere righe che restano nella selezione, e un'azione su qualcosa che non
// si vede e' esattamente cio' che non deve poter succedere.
std::vector<std::wstring> SelectedPaths(const AppState& app);
std::vector<const BYTE*> SelectedPidls(const AppState& app);

// **Dove sta una voce**: un solo posto decide, cosi' aprire, selezionare e chiedere
// un verbo alla shell non possono divergere sul significato di "questo file".
std::wstring EntryLocation(const AppState& app, const Entry& entry);

// L'icona di una voce. Per quasi tutte e' quella della **sua estensione**, che la
// shell risolve senza toccare il disco; per la radice di un volume dipende dal disco,
// e allora si chiede guardandola.
const ShellIconAtlas::IconUV* IconFor(AppState& app, const Entry& entry);
} // namespace Bolt
