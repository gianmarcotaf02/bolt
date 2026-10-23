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

// Il titolo di una colonna **come si legge adesso**. Per tutte tranne i quattro
// segnaposto e' `ColumnLabel(i)`; per un `Empty` e' il titolo della colonna che sta
// mostrando, perche' una colonna intitolata «Vuota 2» che mostra il tipo e' una
// colonna che mente sul proprio contenuto.
std::string ColumnTitle(const AppState& app, int column);

// Il nome come si **scrive**: con `ShowFileExtension` spento, senza estensione.
// Il nome vero resta quello della voce, e questa e' l'unica differenza fra i due.
std::string DisplayName(const AppState& app, const Entry& entry);

// --- colonne e viste ---------------------------------------------------------
void InitColumns(AppState& app);

// Le proprieta' del volume della cartella aperta — spazio libero, occupazione, file
// system — chieste una volta a ogni cartella e tenute in `AppState::volumeFacts`.
void RefreshVolumeFacts(AppState& app);
void SetViewMode(AppState& app, ViewMode mode);

// Quali colonne si vedono. Le due date che arrivano insieme alla modifica —
// creazione e ultimo accesso — si spengono dalle opzioni e di partenza sono
// **spente**: tre date affiancate si prendono lo spazio dei nomi per dire quasi
// sempre la stessa cosa. Le altre non si spengono, e la funzione lo dice.
bool ColumnVisible(const AppState& app, int column);

// L'indice della colonna che ordina su |sort|, o -1: cosi' chi accende o spegne una
// colonna la nomina per cosa mostra e non per la sua posizione nella tabella.
int  ColumnOfSort(SortColumn sort);

// I bordi delle colonne, in frazioni della larghezza dell'elenco: `edges[i]` e' il
// bordo sinistro della colonna `i`, `edges[kColumnCount]` la fine dell'elenco. Una
// colonna nascosta **non occupa spazio** — il suo bordo si porta su quello della
// prima visibile che la segue — e lo spazio va alla colonna prima. Le frazioni
// salvate restano quelle scelte dall'utente: qui si legge, non si scrive.
void ColumnEdges(const AppState& app, float* edges);

int  NextVisibleColumn(const AppState& app, int column);
int  PrevVisibleColumn(const AppState& app, int column);

// Accende o spegne una colonna. Spegnerla non lascia niente di invisibile acceso:
// il filtro per valori di quella colonna si toglie, e se era lei a ordinare si torna
// al nome — un ordine che nessuna intestazione spiega e' peggio di nessun ordine.
void SetColumnVisible(AppState& app, int column, bool visible);

// "Adatta": la larghezza di ogni colonna misurata sul suo contenuto, con il nome che
// prende quello che resta. E' il valore di partenza delle colonne; il trascinamento di
// un bordo lo spegne (`autoFitColumns`), il doppio clic su un bordo lo riaccende.
void FitColumns(AppState& app);

// "Adatta questa colonna": la larghezza misurata sul contenuto di |column|, le altre
// dove stanno. E' il comando che il riferimento mette nel menu del tasto destro
// sull'intestazione, dove l'imbuto invece sceglie i valori.
void FitColumn(AppState& app, int column);

// "Sposta questa colonna": -1 a sinistra, +1 a destra. Scambia il posto con la
// vicina e ognuna tiene la sua larghezza. E' lo `Shift Column Left/Right` del
// riferimento.
void ShiftColumn(AppState& app, int column, int direction);

// "Ripristina le colonne": larghezze, visibilita' e filtri di partenza, come in
// un'applicazione appena nata. Il nome lo dice: si torna indietro, non si adatta.
void ResetColumns(AppState& app);

// I **layout per cartella** (`DirHostLayoutSave`/`DirHostLayoutDel`, M9), in memoria.
// Riflettono `AppState` in un valore, e quel valore lo rimettono: sono le due
// direzioni della stessa cosa, come `RefreshOptionItem`/`ApplyOptionFromItem`.
void SaveFolderLayout(AppState& app);      // la cartella aperta ricorda com'e' guardata
void ForgetFolderLayout(AppState& app);    // e se ne dimentica
bool ApplyFolderLayout(AppState& app);     // vero se quella cartella ne aveva uno
bool HasFolderLayout(const AppState& app); // vero se ce n'e' uno da applicare

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

// --- muovere la selezione e la vista -----------------------------------------
//
// La selezione la cambia chi la disegna e chi riceve il clic, e le due cose devono
// voler dire lo stesso: stavano in main.cpp e le usavano solo il disegno e l'input.
// Uscite con l'input, il 2026-10-23, perche' l'input ha bisogno di una selezione sola.
void SelectOnly(AppState& app, int entryIndex);
void ToggleSelection(AppState& app, int entryIndex);
int  RowOfEntry(const AppState& app, int entryIndex);   // -1 se la vista la nasconde
void SelectRowRange(AppState& app, int rowFrom, int rowTo);
void ApplyRectSelection(AppState& app);                 // il rettangolo, adesso
void FinishRectSelection(AppState& app);                // fine del trascinamento
void ScrollBy(AppState& app, const Layout& layout, float delta);
void MoveSelection(AppState& app, int delta, bool extend);
void EnsureSelectionVisible(AppState& app, const Layout& layout);
} // namespace Bolt
