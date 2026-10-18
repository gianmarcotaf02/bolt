# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

Ultimo aggiornamento: **2026-10-16**, dopo la **mappa dei tasti** (M9) e il suo
**editor**. Le scorciatoie non sono piu' quarantasei `case` dentro `WindowProc` ma una
tabella in `src/keymap.*`, e il tasto chiama **lo stesso** `RunCommand` della palette
(le due copie dello stesso corpo sono sparite). Le righe di fondo dei menu si scrivono
da lei. E l'**editor** adesso c'e': una riga `Key Map` fra le opzioni (e
`CmdEditKeyMap` nella palette) apre i comandi col tasto accanto, e da li' si rimappa,
si toglie e si torna ai tasti di partenza — `docs/04` M9 ha cosa fa e i limiti che
dichiara. Nella stessa sessione l'autore ha **chiesto** perché la palette dei comandi
fosse ancora in inglese con la lingua su italiano, e la risposta era in tre posti:
le **bande dei gruppi** di ogni menu, le **righe del pannello delle opzioni** e
**un comando** su ottanta — tutte cose che si disegnavano da una **chiave** del
riferimento invece che da una frase, e che il paragrafo «Cosa resta in inglese» di
`docs/04` diceva di lasciare in inglese. Adesso sono tradotte e la chiave non si e'
mossa: chiuso `Vista` in italiano, in inglese la banda si chiama `View` ed e' **ancora
chiusa**. Vedi `docs/02` §7.56. Poi, **sempre in ordine**, il **fissaggio in qualunque
sezione** (M6): `Fissa in...` e `Togli da...` mettono una cartella in accesso rapido o
nei segnalibri, e nell'accesso rapido le nostre righe stanno in cima a quelle di
Windows con la stella che le distingue — `docs/04` M6, e `docs/02` §7.57 per il difetto
che si e' pagato strada facendo. **Poi il trascinamento fuori dal cestino** (§7.34),
che era il primo della lista come *diagnosi*: il difetto non c'era piu' — una voce
riciclata trascinata in una cartella si ripristina li' — e i due giri precedenti
avevano sbagliato per colpa del **banco**, non di OLE (il primo piano non vinto, e una
riga misurata a occhio invece che a pixel). Nel farlo e' successa una cosa che vale piu'
della diagnosi: **una finestra di prova e' nata sulla scrivania virtuale su cui l'autore
stava studiando**, e portarla in primo piano gliel'ha messa davanti. Da li' la regola
nuova in `AGENTS.md` §0 — la scrivania di chi lavora non e' un banco di prova — e
`tools/focus-window.ps1`, che si rifiuta di attivare una finestra che non sta sulla
scrivania corrente. L'autore aveva anche
**deciso**: per **M6** il fissaggio vale per **qualunque sezione** (accesso rapido,
segnalibri, o anche una cartella standard come Desktop), con le due conseguenze
scritte in `docs/04` — la lista della shell non si scrive, e i fissaggi restano in
memoria; **M4, M7 e M9** restano come sono. Prima, il secondo blocco di
**spezzamento**: la **barra di stato** (`src/view_statusbar.*`) e i **menu a gruppi**
(`src/view_menu.*`, 535 righe), e `main.cpp` da 8.080 a **7.414**. Prima,
l'**ispettore** intero (`src/view_inspector.*`, 1.053 righe), che l'aveva portato da
9.134 a 8.080. Prima, il **contenuto della
cartella nell'ispettore**: scegliendo una cartella, il pannello ne mostra in fondo le
prime voci **in elenco**, con la loro icona o miniatura, **senza aprirla** (funzione
chiesta dall'autore; `PeekEntry` in `src/app_state.h`). Prima, il **dialogo di
progressione di M7**:
copia, spostamento e cestino girano su un **thread di lavoro** con il conto dei byte,
la voce in corso e un annullamento che ferma davvero (`FileOps::Job` in
`src/file_ops.*`, il pannello in `main.cpp`). E' la prima cosa di M7 che si tocca con
le mani — prima l'operazione teneva ferma la finestra — e ha portato con se' due
cose che il documento di progetto dava per vere e non lo erano: `IFileOperation`
**non ha** un `Abort`, e i "punti" di `UpdateProgress` **non sono** byte (§5).
Prima, il primo **spezzamento di `main.cpp`**: il confine in
`src/app_state.h`, i supporti in `src/app_ui.*`, e fuori quattro moduli —
`view_sidebar`, `view_titlebar`, `tabs` e la **lista con le colonne**,
`src/view_list.*`. Poi il **tema chiaro**
(`src/theme.*`: i ruoli di colore che hanno un lettore, le due tavolozze, la scelta
dal pannello e dalla palette, `SystemColorScheme` che segue Windows anche a
finestra aperta) e **tre difetti trovati mentre si faceva**: il **suggerimento che
leggeva la riga 1 di 1** (`docs/02` §7.51, una caduta riproducibile entrando in una
cartella dopo aver scelto un file di testo), il **backend Direct3D 11 che non
poteva partire** (§7.52, `point` come nome di parametro in HLSL) e il **pidl
liberato e poi usato** (§7.53, in `ShellLocation::Read`: il ripristino dal cestino
riceveva l'identita' di un blocco riusabile). Prima: la
**sorveglianza delle cartelle** (M3 chiusa: i numeri si aggiornano da soli, §7.47 e
§7.48), le **linguette come elementi** invece che
come caratteri (un triangolo nello shader, §7.49) e i **pulsanti della barra del
titolo** come forme (piu' e croce, §7.50), e il **corpo dei dettagli piu' piccolo**
di 4 px sotto il corpo principale, derivato (§7.12). Prima ancora: il
**pannello delle opzioni** (M9, `Ctrl+,`), l'**allineamento delle scorciatoie**,
la **lingua su tutte le superfici**, la **salita da una radice di volume**, le
**tre cose viste nell'originale**, l'**albero dei tab verificato**, le **icone**
— oggetto, applicazione, e grandi alla misura giusta (§7.43, §7.44,
§7.45) —, i **byte allocati** (§7.46), **`InspectorHideGrid`** e il **backend
Direct3D 11** (M10).
Registro: **cinquantasette** voci, **cinquantacinque risolte** e **due aperte** —
l'ispettore che mostra una voce nascosta dal filtro (§7.55) e il backend Direct3D
spento apposta (§7.52), che aspettano **una decisione**, non una diagnosi. La caduta
rara all'avvio (§7.54) e' chiusa il 2026-10-18: era §7.53 visto dall'altro lato, con
la pila che era gia' in `%LOCALAPPDATA%\CrashDumps`. §7.34 non ha piu' un residuo
dichiarato: il trascinamento fuori dal cestino e' stato visto funzionare. **Una** cosa
da fare, piu' quelle che vogliono una sessione propria: i limiti dichiarati stanno dove
vivono.

---

## 1. Dove siamo

| Milestone | Stato |
|---|---|
| M1 — shell e rendering | ✅ |
| M2 — icone e anteprime | 🟡 oggetti e applicazioni fatti, manca la resa monocromatica (provata e tolta su richiesta dell'autore) |
| M2b — navigazione | ✅ |
| M3 — statistiche di cartella | ✅ byte logici e **allocati**, e i numeri si aggiornano da soli quando la cartella cambia da fuori (`ReadDirectoryChangesW`, con il limite dichiarato di due livelli) |
| M3b — attributi NTFS | ✅ il record del file letto dal volume raw (`FSCTL_GET_NTFS_FILE_RECORD`), i nomi della tabella `AT_*`, la lista nell'ispettore e la degradazione dichiarata senza privilegi (`src/ntfs_attributes.*`, `docs/04` M3b) |
| M5 — filtro, ordinamento, raggruppamento, selezione | ✅ |
| M5c — modalità di visualizzazione | ✅ quattro viste, celle **derivate** da una misura continua (icona, larghezza e altezza), raggruppamento in griglia e nell'elenco, ridimensionamento continuo con `Ctrl`+rotella, **selettori di vista e misura delle anteprime nella barra in fondo**, e **icone grandi chieste alla misura giusta** invece di ingrandite (`docs/02` §7.45) |
| M5d — sovrappasso e comportamento del clic | ✅ evidenzia righe e celle, e le otto chiavi di interazione: `HoverToSelect`, `UnderlineOnHover`, `ShowTooltips`, `SingleClick`, `SingleClickFoldersOnly`, `ActOnPress`, `DoubleClickToParent`, `ShowCheckboxes` |
| M5b — colonne | ✅ ridimensionabili col mouse, con filtri per valore nelle intestazioni e **filtro di testo sul nome** |
| M6 — tab, barra del titolo, palette, GoTo | ✅ tab, titolo, palette, GoTo, **barra laterale a sezioni** (fissaggi, recenti, segnalibri, volumi, cartelle standard), **pulsanti di navigazione**, **albero dei tab verificato** (creazione per trascinamento, chiusura per ramo, distacco) e il **fissaggio in qualunque sezione**: `Fissa in...` / `Togli da...` (palette e menu contestuale) mettono una cartella in accesso rapido o nei segnalibri, e nell'accesso rapido le nostre righe stanno in cima a quelle della shell con la stella che le distingue. I limiti sono dichiarati in `docs/04` M6 (i comandi agiscono sulla cartella aperta, il toglimento non ha un gesto sulla riga, i fissaggi sono di sessione) |
| M7 — operazioni su file | 🟡 copia, sposta, rinomina, cestino, clipboard interoperabile, **menu contestuale a più livelli** con **`Proprietà` in-app** e **`Proprietà di Windows`**, le **voci di primo livello della shell**, il **menu della cartella** sul fondo, i **sottomenu delle estensioni**, **`Nuovo file >`** (tipi dal registro, creazione con
contenuto), **`Invia a >`** (destinazioni della cartella SendTo, eseguite lasciandoci
cadere sopra la selezione), **drag&drop dentro e fuori** con OLE, la **navigazione
nello spazio dei nomi** col **ripristino dal cestino**, il **trascinamento sul
riquadro di navigazione** (che sul cestino è il gesto per buttare via, non una
copia) e il **dialogo di progressione**: copia, spostamento e cestino su un thread di
lavoro, il conto dei byte, la voce in corso, `Esc` e `Annulla` che fermano davvero, e
il file parziale che la shell cancella da sola, e il **trascinamento fuori dal
cestino** (una voce riciclata trascinata in una cartella **si ripristina** li': visto
il 2026-10-17, `docs/02` §7.34). Mancano le cartelle riciclate, che si ripristinano e
non si aprono |
| M8 — inspector | ✅ metadati, proprietà `PROPSYS`, conteggio per tipo, **visualizzatore di testo** (righe, minimap, spazi visibili, andare a capo, ingrandimento, codifica), **anteprima immagine** decodificata con adattamento e ingrandimento, **griglia di sfondo** (`InspectorHideGrid`) e il **contenuto della cartella scelta** in fondo al pannello, **in elenco** (le prime voci con la loro icona o miniatura, senza aprirla, **a richiesta dell'autore**). Le voci di spazio dei nomi mostrano le colonne della shell (percorso originale, data di eliminazione) |
| M9 — configurazione e temi | 🟡 pannello delle opzioni (`Ctrl+,`) con i controlli tipizzati, preferenze ricordate in `%APPDATA%\Bolt\settings.ini`, **lingua su tutte le superfici** — comandi, bande dei gruppi di ogni menu, righe delle opzioni, barra laterale, colonne, inspector, con la **chiave** che resta l'inglese (`docs/02` §7.56) —, **tavolozza chiara e scura** con `SystemColorScheme` che segue Windows e l'**editor della mappa dei tasti** (la riga `Key Map` fra le opzioni, `CmdEditKeyMap` nella palette: elenco dei comandi col tasto accanto, cattura del tasto nuovo, `Canc` che toglie, tasti di partenza in cima); mancano le viste per cartella e i layout salvabili |
| M10 — backend D3D11 | 🟡 `IRenderer` dietro cui stanno **OpenGL** e **Direct3D 11** (stessi shader in HLSL, stesso batcher di quadrilateri), con scelta all'avvio (`BOLT_BACKEND`) e fallback all'OpenGL. Lo shader HLSL incorporato pero' **non compila** — `point` e' una parola riservata — quindi il Direct3D non e' mai partito davvero e il fallback faceva sembrare tutto a posto. Il rimedio e' una parola e non e' applicato: accenderebbe il secondo backend **per default**, e quel percorso non ha avuto un giro completo. `docs/02` §7.52 |

L'applicazione è **usabile**: si naviga, si filtra, si raggruppa, si seleziona, si
aprono tab, si salta con GoTo, si guardano anteprime e statistiche ricorsive, e si
lavorano i file — inclusa l'apertura delle proprietà di Windows.

---

## 2. Debito tecnico dichiarato

Ordinato per quanto morde.

0. **La caduta rara all'avvio — chiusa il 2026-10-18.** `docs/02` §7.54: i nove dump
   gia' presenti in `%LOCALAPPDATA%\CrashDumps` mostrano quattro cadute dentro
   `windows.storage.dll`, tre delle quali **in `ILGetSize`** chiamata dal nostro
   codice: era §7.53 visto dall'altro lato, un pidl liberato e poi letto. Rimettendo
   l'ordine di prima la caduta torna (6 su 120 avvii), togliendolo sparisce (0 su
   515). Resta dichiarata, e non riprodotta, una **seconda famiglia** di dump caduti
   dentro il nostro eseguibile (cinque, tutti prima del 2026-10-16): vedi la voce.
1. **`src/main.cpp` e' a 4.816 righe, ed erano 11.394** — scesa a 7.344 con lo
   spezzamento, risalita a 7.895 con l'editor dei tasti e il fissaggio, e scesa di
   **3.079** il 2026-10-18 in cinque tagli: `src/view_options.*` (764 righe, con cinque
   funzioni andate nel modulo a cui appartenevano), **`src/shell_menu.*`** (537 righe:
   i verbi della shell e la loro cache) e **`src/view_context_menu.*`** (1.078 righe: la
   superficie del menu contestuale — geometria, livelli, disegno, clic, tasti — coi sei
   comandi che chiama) e **`src/settings.*`** (213 righe: leggere, scrivere e ricordare
   le preferenze, che era il pezzo con meno dipendenze rimasto li'), **`src/navigation.*`**
   (538 righe: leggere una cartella, la storia avanti/indietro, l'albero dei tab) e
   **`src/input_targets.*`** (il registro dei bersagli, spostato **perche'** la
   navigazione lo chiedeva: un modulo non puo' chiamare una funzione interna a un
   altro), piu' le due domande sulla selezione
   (`SelectedPaths`/`SelectedPidls`) passate a `view_list`, che e' il modulo che
   risponde gia' a "cosa e' selezionato". Sono usciti **dieci** moduli di vista e
   modello — `src/view_sidebar.*`, `src/view_titlebar.*`, la **lista con le colonne**
   (`src/view_list.*`), la parte pura dei tab (`src/tabs.*`), **l'ispettore intero**
   (`src/view_inspector.*`), la **barra di stato** (`src/view_statusbar.*`), i **menu a
   gruppi** (`src/view_menu.*`), il **pannello delle opzioni** (`src/view_options.*`),
   i **verbi della shell** (`src/shell_menu.*`) e il **menu contestuale**
   (`src/view_context_menu.*`), le **impostazioni** (`src/settings.*`), la
   **navigazione** (`src/navigation.*`) e il **registro dei bersagli**
   (`src/input_targets.*`) — piu' i supporti (`src/app_ui.*`) e il confine
   (`src/app_state.h`).

   **La decisione sul confine, presa il 2026-10-18.** `WindowProc` non si sposta: e' la
   colla della finestra, e le sue 1.319 righe non sono una unita' ma **i suoi `case`** —
   `WM_LBUTTONDOWN` 319 righe, `WM_KEYDOWN` 296, `WM_MOUSEMOVE` 226. Va spezzato **per
   messaggio**, e ogni messaggio va dove sta la cosa che quel messaggio muove. Ma il
   corpo di un `case` **e'** l'azione della superficie che riceve l'evento: finche' la
   superficie e' una funzione interna a `main.cpp`, spostare il `case` vuol dire
   trascinarsi dietro la superficie. Quindi l'ordine e':

   1. **prima le superfici** — una per volta, come si e' fatto finora (`view_menu` e,
      il 2026-10-18, `view_options`);
   2. **poi gli eventi** — con le superfici fuori, `WM_KEYDOWN` puo' diventare
      `input_keys.*` e i messaggi del mouse `input_mouse.*`, perche' non hanno piu'
      niente da trascinarsi;
   3. **`main.cpp` resta la finestra**: `wWinMain`, la classe, i messaggi che
      riguardano la finestra e basta (`WM_NCCALCSIZE`, `WM_NCHITTEST`, `WM_SIZE`,
      `WM_SETTINGCHANGE`, `WM_DESTROY`) e lo `switch` che smista agli altri.

   **La cucitura, dichiarata invece che implicita.** Un modulo che ha bisogno di una
   funzione rimasta in `main.cpp` non puo' chiamarla: e' interna. La scelta non e' fra
   chiamarla e non chiamarla, e' fra **dichiararla** e lasciarla invisibile, e allora
   c'e' `src/app_actions.h`: l'elenco delle azioni ancora dentro il file, con accanto il
   modulo che dovrebbero avere (`RememberSettings` → `settings.*`,
   `BuildKeyMapMenu`/`CloseKeyMap` → `view_keymap.*`, `ClosePinMenu` → `view_pins.*`).

   Il 2026-10-18 la lista e' cresciuta di sei voci — `RunCommand`,
   `ActivateSelection`, `ReloadKeeping`, `ComputeLayout`, `OpenPropertiesPanel`,
   `CreateNewItem` — e non e' un errore: la superficie del menu contestuale *chiede ai
   comandi di fare*, e i comandi non hanno ancora un modulo. La regola resta quella, ma
   il conto va letto nell'ordine giusto: la lista si accorcia **quando il pezzo che
   manca arriva**, e il pezzo che manca qui e' `src/commands.*` — `RunCommand` e i
   `Do*`, che sono anche l'ultima cosa che `WindowProc` chiama. Se dopo quello la lista
   non si accorcia, la cucitura e' nel posto sbagliato. Intanto si e' gia' accorciata di
   **due**: `RememberSettings` e' uscita con `src/settings.*` e `ReloadKeeping` con
   `src/navigation.*` — i due casi in cui la cucitura funzionava come previsto, un pezzo
   che trova il suo modulo e sparisce dall'elenco.

   Restano dentro **i comandi e l'input**: i **comandi** (`RunCommand` e i `Do*`, piu' le
   azioni che il menu chiama) e i `case` di `WindowProc`. Le superfici sono fuori tutte,
   quindi il passo 2 della decisione e' **sbloccato**: `WM_KEYDOWN` e i messaggi del
   mouse possono diventare `input_keys.*` e `input_mouse.*` senza trascinarsi niente
   dietro. I confini dei componenti ci sono (`GroupMenu`,
   `ContextMenu`, `file_ops`, `dir_stats`, `shell_*`, `drag_drop`) ma quelli della vista
   vivono ancora nello stesso file. Spostarli e' un lavoro meccanico ma largo, e va
   fatto quando c'e' spazio per riverificare tutto: il numero era **risalito** due volte
   — 156 righe col tema, 302 col dialogo di progressione, 200 col contenuto della
   cartella — ed e' il prezzo di scrivere un pezzo nuovo dove c'e' ancora il collo di
   bottiglia. Spostarne uno subito dopo costa meno che aspettare.
2. **Nessun controllo di versione locale.** Il repository è su GitHub
   (`gianmarcotaf02/bolt`) e il sistema salva con commit automatici a ogni turno,
   ma non c'è una convenzione di branch: si lavora su `main`.
3. **Il ritaglio costa due disegni per area.** Il posto giusto per un ritaglio è il
   **quad**, valutato nello shader: riporterebbe il fotogramma a un disegno solo e
   renderebbe il ritaglio una proprietà di chi disegna invece che dell'ordine in cui
   si disegna. Vedi `AGENTS.md` §8.
4. **Lo zoom ricampiona il testo.** A `scale` diverso da 1.0 sia il corpo che il
   secondo corpo vengono scalati, quindi il ricampionamento torna. Eliminarlo
   richiede una rasterizzazione a runtime per ogni dimensione: è un altro
   mestiere, non un difetto da correggere adesso.
5. **Una sola operazione su file per volta, e il conto dei byte interpolato.** Il
   dialogo di progressione non ha una **coda**: una seconda caduta mentre la prima
   gira viene rifiutata — dichiarando `DROPEFFECT_NONE`, cosi' l'origine non cancella
   niente — invece di essere messa in fila come fa Esplora risorse. E i byte mostrati
   sono la frazione dei **punti** della shell moltiplicata per i byte che `Job` ha
   misurato prima di partire: **esatti per un file solo**, interpolati su tante voci
   piccole, perche' i punti sono proporzionali al tempo e non ai byte. Entrambi sono
   scritti nel codice (`src/file_ops.cpp`) e misurati in `bolt-diag.log`.
6. **Il componente dei menu non sa disegnare una riga spenta: lo sa solo il pannello
   delle opzioni.** `GroupMenuItem::enabled` lo legge il ramo `richRows` e i tasti
   delle opzioni; in un menu normale la riga spenta si disegna come le altre e si
   **attiva** lo stesso. Oggi non e' un difetto visibile — nessun menu normale ha
   righe spente — ma il fissaggio ci e' arrivato vicino: le due destinazioni che non
   cambierebbero niente dicono la ragione **nel nome** (`Segnalibri (non e' fissata
   qui)`) perche' il meccanismo non c'era. Il secondo utilizzatore e' arrivato: il
   prossimo menu che ha bisogno di una riga spenta lo paga.

---

## 3. Prossimi passi candidati

**Tre numeri, che sono tre cose diverse e non vanno confusi.**

- **Cinquantasette** voci nel registro `docs/02` §7, una per una con sintomo, causa e
  correzione: **cinquantacinque risolte**, e le due che restano aperte — §7.52 e §7.55
  — aspettano una **decisione**, non una diagnosi. È un cumulativo, non una lista di
  lavoro.
- **Due** cose **da fare**: la lista qui sotto.
- I **limiti dichiarati**: cose che si è scelto di non fare, scritte dove vivono con
  la ragione accanto (il tetto di 1 MB nella lettura dei testi, i modelli `ShellNew`
  con valore `Command`, le 23 colonne della specifica contro le 6 fatte, il ritaglio
  per-quad in `AGENTS.md` §8, le sei opzioni che il pannello mostra spente). Ogni
  volta che nei documenti c'è scritto "dichiarato", vuol dire questo: non risolto, e
  detto.

Alcune voci portano con sé un residuo dichiarato — §7.34 ne aveva uno, la voce
riciclata che non veniva presa, e il 2026-10-17 si è visto che non c'era più: una voce
riciclata trascinata fuori si ripristina — e il conteggio non lo nasconde, perché il
residuo è scritto nella voce stessa e sparisce quando sparisce.

In ordine di valore, non di facilità.

> **M9 — il pannello delle opzioni** (`Ctrl+,`), **l'allineamento delle scorciatoie**
> (`Ctrl+P`/`F4` al GoTo, `Ctrl+Shift+P` ai comandi, `Ctrl+,` alle opzioni), **finire
> la lingua**, **salire da una radice di volume**, **le tre cose viste nell'originale**
> (selettori di vista e anteprime in fondo, filtro del nome, `RECENTS`),
> **l'albero dei tab**, **`InspectorHideGrid`**, il **backend Direct3D 11** e il
> **tema chiaro** erano voci
> di questa lista, e sono fatte: vedi `docs/04` M9, M6, M8 e M10, e §7.40/§7.41/§7.42
> di `docs/02`. Restano della M9 le **viste per cartella** e i **layout salvabili**
> (la **mappa tasti rimappabile** ha il suo editor: vedi sotto); di M4 il
> **punteggio** dei recenti, che adesso e' solo l'ordine
> di visita.

### L'ordine deciso il 2026-10-16, e dove siamo

**Fatto per primo, com'era nell'ordine: l'editore della mappa dei tasti** (M9,
`docs/04`). La mappa era gia' un dato, e `Rebind`/`Clear`/`Reset`/`Bindings()` erano
scritti e non li chiamava nessuno; adesso c'e' il posto da cui rimappare. Una riga
`Key Map` fra le opzioni (lo stesso comando, `CmdEditKeyMap`, e' anche nella palette)
apre l'elenco dei comandi col tasto accanto; `Invio` comincia la cattura del tasto
(«premi un tasto per *Rinomina*»), `Esc` annulla, `Canc` toglie, e i tasti di partenza
sono la prima riga dell'elenco. Verificato a schermo, non a impressione: rimappato
`Rinomina` su un tasto nuovo e visto quel tasto rinominare davvero, e il vecchio non
piu'; tolto un tasto con `Canc`; rubato `F5` ad `Aggiorna`; rimesso tutto con la riga
dei tasti di partenza, con l'esito nella barra di stato. I limiti — mappa di sessione,
un tasto da scrivere sciolto che non si assegna, e l'avviso quando si sposta il tasto
che apre l'elenco — sono scritti in `docs/04` M9.

**Poi, com'era nell'ordine: il fissaggio in qualunque sezione** (M6, `docs/04`). Le due
decisioni che andavano prese prima di scrivere sono applicate: i fissaggi sono
**nostri** (la lista della shell non si tocca) e vivono **in memoria**, per la
sessione. `Fissa in...` e `Togli da...`, dalla palette e dal menu contestuale, mettono
una cartella in **accesso rapido** o nei **segnalibri**; nell'accesso rapido le nostre
righe stanno in cima a quelle della shell, con la stella che le distingue. Verificato a
schermo: quattro cartelle fissate, la stella della barra strumenti che si accende
fissando nei segnalibri, una riga tolta con `Togli da...` e sparita dalla barra
laterale, la riga fissata che porta alla cartella cliccandola, le due voci presenti sul
fondo del menu contestuale e **non** con una selezione, e le stesse schermate in
inglese. La decisione (b) — scrivere il jump list di Windows — non si e' provata e non
serve: il verbo della shell per farlo c'e' gia' nel menu contestuale (`Aggiungi a
Preferiti`) e lo scrive **il sistema**. I limiti sono in `docs/04` M6; il difetto
trovato strada facendo (un menu nuovo si registra in quattro posti, e me n'era sfuggito
uno) e' in `docs/02` §7.57.

**Poi il trascinamento fuori dal cestino** (§7.34), che era il primo della lista come
*diagnosi*: e' **fatto**, e il difetto non c'era piu'. Una voce riciclata trascinata in
una cartella si ripristina li' — 28 byte, contenuto identico, sparita dal cestino,
`DROPEFFECT_MOVE` nel registro — e una prova di controllo con un file normale arriva
uguale. I due tentativi precedenti avevano concluso il contrario per colpa del **banco**:
`SetForegroundWindow` non bastava a vincere il primo piano, e la riga sotto il puntatore
era stata misurata a occhio da una cattura piccola. Adesso c'e'
`tools/focus-window.ps1`. Una prova sola per caso, pero': la ripetizione e' saltata
perche' la finestra di prova era nata su un'altra scrivania virtuale (`AGENTS.md` §0).

**Il prossimo lavoro, in ordine** — deciso il 2026-10-18, dopo che lo spezzamento e'
arrivato a un punto in cui il file non e' piu' il problema principale:

1. **Chiudere il Direct3D 11** (`docs/02` §7.52, `docs/04` M10). E' l'unico punto in cui
   il programma **dice il falso**: si dichiara con due backend e ne gira uno, e l'altro
   fallisce in silenzio — visibile solo nel registro. La correzione e' una parola
   (`point` e' riservata in HLSL, il parametro delle funzioni di distanza va chiamato
   `p`), la parte che conta e' il **giro di verifica** con `BOLT_BACKEND=d3d11`: le
   superfici da guardare sono gia' un copione (lista, menu contestuale, opzioni,
   ispettore, mappa dei tasti, tema chiaro) e le catture di riferimento esistono. Se
   regge, il Direct3D diventa il backend di partenza e la milestone e' vera.
2. **Le due piccole bugie**, che portano il **registro dei difetti a zero**:
   **§7.55** — l'ispettore che mostra una voce che il filtro ha nascosto: la
   raccomandazione e' far **spostare il cursore** sulla prima riga visibile, come fa
   Esplora, invece di insegnare all'ispettore a tacere; tocca la selezione, quindi e'
   una decisione dell'autore, non un'improvvisazione. E il **suggerimento disegnato
   sopra il pannello delle proprieta'**, che vuole una regola sola: i suggerimenti
   tacciono quando un pannello e' aperto.
3. **`src/commands.*`** — `RunCommand` e i `Do*` fuori da `main.cpp`. Non per il numero:
   perche' **M4 ci passa dentro** (`Cancella recenti`, i menu), e spostare un pezzo
   mentre ci si lavora costa meno che spostarlo dopo. Oggi chiede **13 voci** di
   cucitura (erano 23 prima di navigazione e tab).
4. **M4 — il ranking dei recenti**, l'ultima lacuna **funzionale** rispetto al
   riferimento: oggi `RECENTI` e' l'ordine di visita, in memoria. La formula
   (`Score`, `Decay`, `Ordinal`) la specifica non la da': serve REA, e conviene
   cominciare dall'analisi, non dal codice.

**Cosa non si fa adesso, e perche'**: M2 (la resa monocromatica delle icone) e' una
scelta gia' presa; le sei opzioni spente, le 17 colonne mancanti e i `ShellNew` con
`Command` sono limiti **dichiarati**; le **viste per cartella** e i **layout salvabili**
(M9) valgono meno di M4 e vogliono una decisione sulla persistenza, che `docs/06` vieta
di prendere di nascosto.

### L'ordine deciso il 2026-10-16 (storico)

Discusso e scelto con l'autore, in risposta a «come procediamo». In ordine:

1. **Un blocco dello spezzamento: la lista con le colonne** (il pezzo piu' grosso e
   piu' intrecciato: ordinamento, filtri, gruppi, viste, intestazioni passano tutti di
   li') — **fatto**, ed e' `src/view_list.*`. Il residuo e' dichiarato nella voce 1 di
   §2: restano dentro l'inspector, la barra di stato, i menu e l'input.
2. **Il tema chiaro** (M9, i 35 token esistono gia' nella spec: e' cablaggio, non
   disegno) — **fatto**, ed e' `src/theme.*`, con le due tavolozze, la scelta dal
   pannello e dalla palette, `SystemColorScheme` che segue Windows e il riquadro di
   DWM allineato. Il residuo e' dichiarato in `theme.h`: dei 35 token dello spec
   esistono quelli che **hanno un lettore**, gli altri sono elencati e aspettano un
   posto che li legga.
3. **Il dialogo di progressione** di M7 — **fatto**. E' `FileOps::Job`
   (`src/file_ops.*`): copia, spostamento e cestino su un **thread di lavoro**,
   un `IFileOperationProgressSink` registrato con `Advise`, `FOF_SILENT` al posto
   della finestra della shell, e il pannello disegnato da noi con `Esc` e un
   pulsante. Il primo passo scritto qui prevedeva due cose che la misura ha
   corretto: il pulsante **non** chiama `IFileOperation::Abort` — quel metodo non
   esiste — e lo stato non sta in `AppState` ma dentro il `Job`, che e' l'unico a
   doverlo condividere col sink. Verificato nella sandbox con un file da 4 GB: il
   conto si muove **dentro il singolo file** (938 MB → 1,8 GB sulla stessa copia),
   `Esc` e il pulsante fermano davvero, e il file parziale sparisce. Il residuo e'
   dichiarato nella voce 5 di §2.
4. Il motivo dell'ordine: lo spezzamento non si vede, quindi farlo tutto di fila sono
   tre sessioni senza niente da guardare; e cominciarlo **adesso** costa meno di
   cominciarlo dopo, perche' ogni funzione nuova che arriva prima paga il file unico.
5. **Il pannello dell'ispettore fuori da `main.cpp`** — **fatto**, ed e'
   `src/view_inspector.*`: **1.053 righe** uscite intere (metadati, immagine grande,
   visualizzatore di testo, contenuto della cartella), `main.cpp` da **9.134 a
   8.080**. E' uscito **intero** invece che a pezzi perche' i pezzi si chiamano fra
   loro — le righe le costruisce chi le disegna — e dividere a meta' avrebbe voluto
   dire una funzione in due file. Una cosa si e' mossa fuori dal modulo:
   `VolumeOf`, che e' andato in `shell_location` perche' lo chiede anche l'elenco,
   quando decide se una voce e' un'unita'. Verificato con la regola che vale per uno
   spostamento: **le stesse cinque catture di prima, pixel per pixel** (§4,
   `rif-*.png`), e le sedici funzioni contate una per una prima e dopo.
6. **La barra di stato e i menu a gruppi** — **fatto**: `src/view_statusbar.*` (151
   righe) e `src/view_menu.*` (535), `main.cpp` da **8.080 a 7.414**. I menu sono
   usciti come **widget**: stato, filtro, geometria e disegno, con
   `GroupMenuClick`/`GroupMenuKeyDown` che restituiscono un `MenuAction` e non
   eseguono niente. Erano gia' scritti cosi', quindi la logica non e' stata toccata —
   e si vede dal fatto che le **20 funzioni sono identiche al testo di partenza**,
   carattere per carattere, non solo "della stessa lunghezza". Una e' rimasta in
   `main.cpp`: `GroupMenuClick`, che chiede dove sono le righe a `HitTest`, cioe' al
   registro dei bersagli, che sta con l'input. Verificato a schermo su **cinque**
   superfici: palette, opzioni, GoTo, valori di una colonna e menu contestuale
   (§4, `menu-*.png`).
3. **B e C si possono invertire** se si preferisce vedere subito qualcosa di grosso:
   il tema chiaro si tocca con gli occhi, il dialogo di progressione con le mani.

La cosa che resta da fare, e che non e' una diagnosi:

1. **Spezzare `main.cpp`** — **7.116 righe** (erano 11.394, scese a 7.344, risalite a
   7.895, e scese di 780 il 2026-10-18) e ventinove moduli: il confine c'è, e il file
   che lo contiene resta il collo di bottiglia dichiarato fin dall'inizio, ma non piu'
   intatto: la decisione e l'ordine dei prossimi passi stanno nella voce 1 di §2.


---

## 4. Come si costruisce e si verifica

> **Prima di qualunque prova: `AGENTS.md` §0.** Nessun file personale va toccato,
> e nessuna informazione personale va salvata. Le prove si fanno nella sandbox
> del progetto, creata con `tools/make-test-sandbox.ps1`.

Non c'è `cmake` nel PATH della shell. Il percorso è:

```powershell
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build build --config RelWithDebInfo
```

Il ciclo visivo è: costruire, lanciare su una cartella nota, catturare, guardare.

```powershell
./build/RelWithDebInfo/Bolt.exe "D:\una\cartella"
powershell -ExecutionPolicy Bypass -File tools/capture-window.ps1 `
    -ProcessName Bolt -OutputPath analysis/tmp/shot.png
```

Le **shader** si leggono dalla sorgente (`REA_SHADER_DIR`), quindi una modifica a
`src/shaders/*` non richiede ricompilare.

**Una build con AddressSanitizer, per cacciare una caduta.** Serve una cartella di
build separata e il runtime **dinamico** (il CRT statico del progetto non e' compatibile):

```bash
# 1) in CMakeLists.txt, solo per questa build: MSVC_RUNTIME_LIBRARY "MultiThreadedDLL..."
# 2) configurare a parte (build/ e' esclusa dal controllo di versione, build/asan pure)
"$CMAKE" -S . -B build/asan -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_FLAGS=-fsanitize=address
"$CMAKE" --build build/asan --config RelWithDebInfo
# 3) lanciare con l'esito su file: l'applicazione e' WIN32 e non ha una console
ASAN_OPTIONS=log_path=analysis/tmp/asan ./build/asan/RelWithDebInfo/Bolt.exe <cartella>
```

**La cartella `build/asan` va cancellata subito dopo** e `CMakeLists.txt` rimesso
com'era: un eseguibile compilato cosi' **pretende `clang_rt.asan_dynamic-x86_64.dll`**
e, senza, Windows mostra `Bolt.exe - Errore di sistema` — un dialogo che sembra un
guasto del programma e invece e' un residuo di diagnosi. E' successo il 2026-10-16:
il dialogo e' comparso a chi aveva in mano quella build. L'eseguibile buono si
riconosce con `dumpbin -dependents`: non deve nominare `clang_rt`.

`analysis/tmp/` raccoglie i riscontri: `qualita-prima-dopo.png`,
`viste-tiles.png`, `m5-*.png`, `m6-*.png`, `m3-progress.png`, `menu-*.png` (i
verbi della shell: menu aperto, fondo del menu, rotella, finestra ridotta
a 460 px), `list-*.png` (la lista con le colonne: dettagli, elenco, icone, gruppi
nelle due viste, filtro per valore, filtro di testo, scorrimento in griglia),
`theme-*.png`, `ch-*.png`, `sc-*.png` e `d3d-*.png` (il tema: le due tavolozze, il
pannello delle opzioni, la scelta, il filtro della palette, il suggerimento, e il
giro del secondo backend dopo §7.52), e `op*.png` (il dialogo di progressione: il
conto che si muove dentro un file da 4 GB, il pannello annullato col tasto e col
pulsante, lo stato "Operazione annullata", l'incolla normale, l'eliminazione nel
cestino e la caduta su una riga). Le prove con un file grande si fanno **solo** nella
sandbox: `fsutil file createnew analysis\tmp\sandbox\big.bin <byte>` la crea in un
terzo di secondo, e va cancellata prima di lasciare la sessione, e `rif-*.png` e' lo spostamento
dell'ispettore (le stesse catture prima e dopo, da confrontare a pixel), `menu-*.png` sono
le cinque superfici dei menu dopo lo spostamento (palette, opzioni, GoTo, valori di una
colonna, contestuale), `key-*.png` sono i tasti che passano dalla mappa (le due righe di
fondo scritte da lei, la rinomina, l'incolla, le tre viste, la scheda nuova e chiusa,
l'eliminazione), `peek-*.png` e' il
contenuto della cartella nell'ispettore (una cartella con immagini e miniature, una
con dodici voci e la riga «e altre voci», una con due file di testo, una vuota, un
file singolo — dove non c'e' — e sei voci scelte, dove non c'e' perche' l'ispettore
mostra l'aggregato). `split-opzioni.png` e' il pannello delle opzioni dopo lo spostamento in
`src/view_options.*`, ritagliato al solo pannello; `shell-menu-02.png` e' il menu
contestuale di un file dopo lo spostamento dei verbi della shell in
`src/shell_menu.*` — comandi nostri sopra, blocco della shell sotto;
`cm-submenu.png` e' la superficie del menu contestuale dopo lo spostamento in
`src/view_context_menu.*`, con un sottomenu aperto dalla tastiera (`Destra`);
`nav-tab.png` e' la striscia dei tab e la barra del percorso dopo lo spostamento della
navigazione in `src/navigation.*` (una scheda sola, dopo `Ctrl+T` e `Ctrl+W`). `m3b-inspettore-ntfs.png` e
`m3b-degradato.png` sono le due facce degli
attributi NTFS nell'ispettore (la lista, e «serve l'amministratore» senza
privilegi), ritagliate al solo pannello perche' il resto della finestra porta i
nomi dell'accesso rapido di chi lavora (`AGENTS.md` §0). Il banco per una caduta rara all'avvio sta in
`analysis/tmp/crashprobe/` (`probe.exe` lancia l'eseguibile su un desktop Win32
**invisibile**, `dumpscan.exe` legge un dump senza debugger: exception stream,
module list e pila con `StackWalk64`, e `qa.exe` chiama `ShellLocation::Read` in un
ciclo stretto): sono diagnostici, non versionati, e si riscrivono in mezz'ora — la
ricetta sta in `docs/02` §7.54.

---

## 5. Trappole già pagate — non ripagarle

**Cattura schermo.** La finestra deve essere in primo piano o la cattura riprende
ciò che sta sopra. Con `SetWindowPos(h, HWND_TOPMOST, …)` e poi
`HWND_NOTOPMOST` funziona sempre; va tolto il topmost alla fine.

**Il banco di prova deve mostrare ciò che vede l'utente.** `capture-window.ps1`
leggeva `GetClientRect`, quindi ritagliava via l'area non-cliente — e per tutta la
durata di M6 il difetto delle **due barre del titolo** è vissuto esattamente lì,
invisibile a ogni verifica e visibile a chiunque usasse il programma. L'ha
trovato l'utente. Ora il default è la finestra intera e `-ClientOnly` è l'opzione
per i confronti al pixel. Vedi §7.14 di `docs/02`.

**Quando una condizione sembra giusta e il comportamento è sbagliato, scrivi il
valore invece di dedurlo.** `if (wparam == TRUE)` era corretto secondo la
documentazione e secondo ogni esempio in circolazione; il messaggio arrivava con
`wparam` a `FALSE`. Una riga di registro ha risolto quello che il ragionamento
non stava risolvendo.

**Un difetto può nasconderne un altro.** Togliere l'area non-cliente ha tolto
anche ombra e angoli arrotondati: il primo difetto era così evidente da rendere
invisibile il secondo. Dopo aver corretto qualcosa, rimisurare — il bordo
*attorno* alla correzione è la parte che non si guarda mai.

**Input sintetico, tre trappole distinte:**

- `SendKeys` **non** dà il focus a una finestra OpenGL: usare `keybd_event`.
- `keybd_event` va preceduto da `SetForegroundWindow`, altrimenti i tasti vanno
  altrove. Un falso negativo da mancanza di focus sembra un bug dell'app e non lo
  è: **verificare il codice prima di correggerlo**.
- `SetForegroundWindow` **da solo non basta** quando il primo piano ce l'ha un'altra
  applicazione (la chiamata riesce, la finestra non viene davanti, i tasti vanno
  altrove): serve `AttachThreadInput`, che è quello che fa `tools/focus-window.ps1`.
  Lo stesso script **si rifiuta** di agire su una finestra che non sta sulla scrivania
  virtuale corrente, perché attivarla riporterebbe lì chi sta lavorando altrove
  (`AGENTS.md` §0).
- `GetKeyState` **non** vede un modificatore iniettato con `SendMessage`: per i
  tasti con `Ctrl`/`Shift` serve `keybd_event`. Per i tasti senza modificatore
  `SendMessage` con `WM_KEYDOWN`/`WM_CHAR` è più affidabile.
- Un blocco C# in `Add-Type` con una direttiva `using` malformata fallisce in
  silenzio e i tasti non partono mai: sembra che l'app ignori l'input.

**`Alt`+`←`/`→` arrivano come `WM_SYSKEYDOWN`**, non `WM_KEYDOWN`, perché la
finestra ha un menu di sistema.

**Un `Esc` che chiude l'app** è una trappola: corretto, ora azzera il filtro e si
ferma.

**Il pixel più esterno della finestra è vetro, e si vede cosa c'è dietro.** Il
bordo di 1 px che `DwmExtendFrameIntoClientArea` rende trasparente mostra ciò che
sta **dietro** la finestra: spostandola, quel pixel cambia colore. È voluto (ombra e
angoli arrotondati, `docs/02` §7.14), ma vuol dire che un confronto al pixel sul
**bordo** della finestra misura anche il desktop, non solo l'applicazione.

**Le riscritture massive di chiamate annidate duplicano il testo.** Sostituendo
`app.font.Draw(...)` con un secondo atlante, una regex che trova sia la chiamata
esterna sia la `Measure` annidata dentro produce due intervalli **sovrapposti**, e
il secondo incolla il proprio testo sopra la parte già riscritta del primo. Il
compilatore non se ne accorge subito e il danno sembra un difetto di layout.
Prima di riscrivere in massa: cercare i match annidati, e **rileggere il diff**
riga per riga. Il diff ha trovato anche un secondo errore — `scale * 0.5f` in un
centraggio scambiato per una scala di carattere — che nessun test visivo avrebbe
attribuito alla causa giusta.

**Un'applicazione che sembra congelata può essere dentro un ciclo modale.** Due
catture identiche a distanza di secondi distinguono "fermo" da "lento"; una
`SendMessageTimeout` dice se risponde, ma **non** se sta lavorando. Quando l'app
non ridisegna e risponde ai messaggi, la prima cosa da fare è **enumerare le
finestre del processo** e leggerne i titoli: il 2025-10-11 c'era scritto
`OperationStatusWindow — 2 azioni interrotte`, e la finestra che bloccava tutto
non era visibile sullo schermo. Vedi §7.13 di `docs/02`.

**I test devono usare una cartella usa e getta.** Le operazioni di M7 cancellano
davvero: la prova è stata fatta su `analysis/tmp/m7b`, mai su file personali, e la
sandbox va ricreata prima di ogni giro. Cancellare `alfa - Copia.txt` per sbaglio
è il modo in cui un test diventa un danno.

**Una domanda dell'utente può trovare una lacuna che nessun test trova.** Il
sovrappasso del mouse non era nella roadmap: non l'avevo previsto. Lo spec
funzionale dell'originale lo elencava da sempre (`HoverToSelect`,
`UnderlineOnHover`, `ShowTooltips`) e il tema aveva già quattro token dedicati. Il
campo `app.hovered` era **calcolato e mai letto**.

Morale: quando manca una funzione, la domanda da farsi non è «l'avevo prevista?»
ma «lo spec la elencava?». Lo spec è la lista delle cose che mancano; la roadmap è
solo l'ordine in cui pensavo di farle.

**Un campo scritto e mai letto è un difetto silenzioso.** `app.hovered` veniva
assegnato a ogni movimento del mouse e non letto da nessuna parte: nessun warning,
nessun errore, solo una funzione che non c'era. Un `grep` dei lettori, non degli
scrittori, è quello che lo trova.

**Una funzione che non si può scrivere oggi va dichiarata, non improvvisata.** Il
ripristino dal cestino sembra una casella da spuntare e non lo è: il cestino non
ha un percorso, e invocare il verbo sbagliato fra quelli del suo menu contestuale
significa **cancellare davvero**. Il blocco vero è il browsing dello spazio dei
nomi. Vedi `docs/04-roadmap.md`, sezione M7.

**Una voce che risponde `S_OK` può non fare niente.** L'esecuzione di una voce
di un sottomenu della shell restituisce successo e non produce nulla: per
accorgersene bisogna guardare l'**effetto**, e servono due prove — una che non
deve produrre niente e una che deve produrre qualcosa. La seconda dice se lo
strumento sa vedere. Vedi §7.19 di `docs/02`.

**Il numero di voci di un menu non è una costante.** Le estensioni moderne di
Windows 11 si registrano solo dalla seconda apertura, quindi la stessa voce sta a
un indice diverso fra il primo e il secondo tasto destro. Contare le frecce con un
numero fisso sbaglia voce; per una voce precisa si legge il registro diagnostico
dell'apertura in corso, e si naviga con `Fine` e le frecce.

**Un nome di file si prova anche con un accento dentro.** È il difetto che nessuna
prova aveva visto, perché i nomi di prova erano tutti ASCII: `perché.txt` si
disegnava `perch .txt`, e la cosa non dipendeva dal nome del file ma
dall'**atlante dei glifi**, che era ASCII puro (`docs/02` §7.29). Un file manager
italiano senza lettere accentate è rotto per metà dei documenti di chi lo usa, e il
modo di accorgersene è scrivere un nome come lo scriverebbe una persona.

**Trascinare si prova fotografando a metà gesto.** Un trascinamento è un gesto
continuo: la sua evidenza visiva — la destinazione tratteggiata — esiste solo
mentre il tasto è ancora giù, e tenerlo premuto fra due script non si può.
`tools/drag-mouse.ps1` preme, muove a passi, **fotografa**, e solo allora rilascia.
E due finestre si misurano prima di premerci sopra: `CW_USEDEFAULT` sposta la
finestra a ogni avvio, e la prima volta ho trascinato verso un punto che nel
frattempo era diventato un altro.

**Il riquadro di navigazione di Esplora risorse è pieno di nomi personali**: i
ritagli delle prove si tagliano sulla lista dei file, non sulla finestra intera.
Per lo stesso motivo le destinazioni di "Invia a" non finiscono nel registro
diagnostico — si contano, non si trascrivono.

**Un clic è finito nel riquadro di navigazione, e da lì nella cartella personale.**
Provando le caselle di scelta ho cliccato a `x=18` credendo di essere sulla casella:
la lista comincia a `x≈198`, e le prime righe a sinistra sono il riquadro di
navigazione. Il clic ha aperto la cartella dell'utente, e le due catture di verifica
successive hanno preso nomi di cartelle personali. Eliminate subito, e **non erano
versionate** (`analysis/` è escluso dal controllo di versione, verificato con
`git check-ignore`): nessuna copia è entrata nel repository. Rifatta la prova con la
coordinata giusta. La lezione: **prima di cliccare si guarda in che riquadro si
sta cliccando**, non solo il numero.

**Un file di prova è finito sul Desktop di chi lavora.** È successo provando il
trascinamento sul cestino: `QUICK ACCESS` era rimasto aperto, quindi la riga a
quell'`y` non era "Cestino" ma "Desktop", e ci è finito dentro un file finto di
sedici byte. Rimosso subito, per percorso esatto, senza aprire né leggere nient'altro
di quella cartella. Due errori si erano sommati: la sezione non era stata chiusa, e
`send-mouse.ps1` era stato chiamato con coordinate schermo mentre vuole coordinate
cliente — così anche il clic che doveva chiudere la sezione era finito dentro la
lista. Da allora: **prima si chiude `QUICK ACCESS` e si guarda la freccia**, e le
destinazioni sulle voci standard si scelgono solo dopo aver visto il riquadro.

**Il cestino è dati personali, e una cattura li copia.** È successo il
2026-10-12: dopo un ripristino la lista del cestino non era più filtrata — per un
difetto vero, `docs/02` §7.28 — e la cattura ha preso i nomi di quello che qualcuno
ha buttato via. Cancellate subito, e da allora la regola è: nel cestino ci si mette
**un file della sandbox**, si restringe la lista **col filtro**, e solo allora si
fotografa; il ripristino si verifica **contando i file nella sandbox**. Un filtro
che sparisce non è una comodità che manca: è una telecamera accesa in una stanza
che non è la tua.

**Un comando che risponde "fatto" non ha fatto niente.** Due volte, in due
punti diversi: il sottomenu della shell che risponde `S_OK` senza eseguire nulla
(`docs/02` §7.19) e la voce di "Invia a" che `ShellExecuteEx` "avvia" senza darle
niente da fare (§7.22). In tutti e due i casi l'errore non è nel codice — la
chiamata è quella documentata — ma nella **premessa**: si presume che l'esito
riportato corrisponda a qualcosa di osservabile. L'unico modo di accorgersene è
guardare il risultato sul disco e contare i byte.

**Una cattura a dimensione intera inganna sulle cose piccole.** Il campo di
rinomina sul posto (un riquadro scuro con un cursore di **un pixel**) non si
vedeva nella cattura della finestra, e per due volte ho concluso che la rinomina
non fosse partita. Ingrandendo la riga con `PIL` — `crop(...).resize(..., NEAREST)`
— il riquadro e il cursore sono evidenti. Prima di dire "non c'è", si ingrandisce
la zona: vale per i cursori, i bordi di un pixel e i separatori.

**`tools/send-keys.ps1`** esiste perché l'escaping di PowerShell dentro bash
mangiava i caratteri e i tasti non arrivavano: uno script su file è l'unico modo
affidabile di ripetere una prova di input.

**`tools/send-mouse.ps1`** esiste per la stessa ragione, applicata al puntatore:
il menu contestuale si apre col tasto destro, e i tasti `Menu`/`Shift`+`F10` — che
pure ci sono — non verificano la via che usa la gente. Le coordinate sono in area
**cliente**, così la stessa prova si ripete anche se la finestra si sposta. La
rotella vuole `-Clicks` col **segno**, positivo in su: senza, `wheel` fa una tacca
in su, e su una lista già in cima non succede niente — sembra che lo scorrimento
non funzioni. È successo provando lo scorrimento della lista appena estratta, e la
prima conclusione («la rotella non scorre») era sbagliata: il difetto era nel modo
in cui chiamavo lo strumento, non nella lista.

**Chiaro o scuro si legge da un pixel, non dalla miniatura.** In questa sessione ho
detto **due volte** che una cattura era chiara mentre il file diceva `(35, 36, 40)`:
l'immagine che si guarda passando per l'anteprima della conversazione non è la
prova, il pixel lo è. Per dire quale tavolozza sta disegnando l'applicazione si
legge un punto del fondo: `(255, 255, 255)` è la chiara, `(35, 36, 40)` la scura. E
vale per tutto il resto: un colore, un bordo di un pixel, un cursore.

**Due catture si confrontano solo a parità di stato.** Confrontando i due backend
ho concluso che il Direct3D non disegnava i segni degli spazi (`·` e `→`) e invece
l'opzione «segna spazi» era semplicemente **spenta** nel giro nuovo: non è una
preferenza che si ricorda fra le sessioni. La misura l'ha detto: col segno acceso,
le quattro `·` compaiono anche sotto Direct3D. Prima di attribuire una differenza a
ciò che si sta provando, si guarda cos'**altro** è diverso fra i due giri — il
percorso, il filtro, l'opzione, la selezione.

**Una caduta rara non si attribuisce con tre tentativi.** Provando il tema ho visto
l'applicazione sparire all'avvio e ho concluso che era il backend Direct3D: `3 su 8`
contro `0 su 8` in OpenGL. Con più giri la differenza è sparita — `5 su 55` con le
modifiche di questa sessione, `0 su 30` col commit precedente, `0 su 12` sotto
AddressSanitizer. Tre tentativi per parte non distinguono il 40% dal 10%, e una
diagnosi sbagliata manda a cercare il difetto nel posto sbagliato. Per attribuire
una caduta rara servono **campioni**, e per trovarne la causa una **pila** (un
debugger, ASan): se sotto lo strumento non si riproduce, si dichiara con i numeri
che si hanno. La caduta dell'accesso rapido (`docs/02` §7.54) e' stata chiusa cosi',
e vale la pena scrivere come: la pila **c'era gia'**, e la lezione e' nelle due voci
qui sotto.

**Prima di armare un banco, si guarda cosa c'e' gia' nella cartella dei dump.**
`%LOCALAPPDATA%\CrashDumps` conteneva **nove** dump di Bolt, scritti nei giorni in
cui la caduta si misurava, da una configurazione di `LocalDumps` che qualcuno aveva
messo e poi tolto. Tre di quelli danno lo stesso indirizzo dentro
`windows.storage.dll`, e hanno chiuso la questione che un pomeriggio di avvii non
aveva chiuso: cercare li' costa un `ls`, riprodurre una caduta rara costa ore.

**WER rate-limita i dump locali, e in una raffica si mangia proprio quelli che
servono.** Con `LocalDumps\Bolt.exe` configurato in `HKCU` (`DumpType=1`), **sei**
cadute riprodotte di fila non hanno scritto **nessun** dump — nemmeno per un
programma di tre righe che dereferenzia il nullo. La traccia sta in una riga sola,
`HKCU\Software\Microsoft\Windows\Windows Error Reporting\
LastRateLimitedDumpGenerationTime`. Quindi: se i dump non arrivano, non e' detto che
le cadute non ci siano; e i dump che servono si cercano fra quelli **vecchi**.

**Una caduta che AddressSanitizer non riproduce non e' una caduta che non c'e':**
puo' essere nell'allocatore di **qualcun altro**. Il pidl di §7.54 lo alloca la
shell, e ASAN strumenta il CRT nostro: liberare e rileggere un blocco di
`windows.storage.dll` non e' un errore *per ASAN*. `0 su 12` voleva dire «questo
strumento non lo vede», non «non c'e'».

**Un banco che legge l'accesso rapido stampa i nomi delle cartelle di chi lavora.**
E' successo il 2026-10-18: una prova che serviva a misurare la dimensione dei pidl
ha scritto l'elenco dei fissaggi veri, e il file e' stato cancellato subito. Un
banco di questo tipo scrive **numeri** — quante voci, quanti byte — e non nomi:
la regola di `AGENTS.md` §0 vale anche per un `.log` in `analysis/tmp`.

**Il banco non sa fare ogni gesto, e quando non lo sa lo dice a meta'.** Il
2026-10-18, provando le opzioni: `send-keys.ps1` non ha un'azione per `Ctrl+,`
(`ctrl+oem_comma` risponde `azione sconosciuta`, e **poi** stampa `ok`: leggere la
prima riga, non l'ultima), e `backspace` non svuota il filtro della palette. Le due
strade che funzionano: si arriva alle opzioni dalla **palette** (`Ctrl+Maiusc+P`, poi
`opz`, poi `Invio`), e il filtro si svuota **chiudendo e riaprendo** invece di
cancellare. Aggiungere l'azione `comma` al banco e' la correzione vera, ed e' una
riga.

**La cattura riprende quello che sta sopra, e "procedi" non vuol dire "la macchina e'
libera".** Il 2026-10-18, dopo un'autorizzazione data a inizio sessione, una cattura ha
ripreso la finestra di un'altra applicazione — il materiale di studio di chi lavora —
perche' era in primo piano sopra Bolt: `capture-window.ps1` legge il **rettangolo**
della finestra di Bolt, e dentro quel rettangolo c'era quello che c'era. Il file e'
stato cancellato subito e la preferenza cambiata dalla prova rimessa com'era, ma la
lezione e' l'opposto di una comodita': **la domanda "posso aprire una finestra adesso?"
si rifa' a ogni giro di catture**, non si eredita da un via libera di mezz'ora prima.
Un `GetForegroundWindow` prima di catturare, e la cattura si rifiuta se il primo piano
non e' la finestra che si vuole fotografare, costerebbe una riga — ed e' la correzione
giusta per il banco.

**Un'applicazione elevata non riceve i clic sintetici, e non si chiude con
`taskkill`.** Serve l'amministratore per leggere un volume raw, quindi per provare
M3b Bolt e' stato avviato col prompt `UAC`. Da li' in poi il banco non elevato ha
sbattuto su due muri, tutti e due dell'UIPI: i clic di `send-mouse.ps1` arrivavano
alla finestra e **non facevano niente** (la selezione non cambiava, e sembrava che
l'applicazione ignorasse l'input), e `taskkill` rispondeva `Accesso negato`.
Le due uscite: aprire Bolt **direttamente sulla cartella di prova**, cosi' il
cursore cade sulla prima riga e non c'e' niente da cliccare (ed e' cosi' che la
prova e' stata fatta); e mettere in conto **un `UAC` in piu'** per chiudere, che va
fatto **prima** di avviare, non dopo.

**Il registro diagnostico è il posto giusto per una caduta intermittente.**
L'applicazione spariva entrando in una cartella (`docs/02` §7.51), senza nessun
errore di Windows: il registro, scritto a ogni fotogramma, ha detto l'ultima cosa
fatta prima della morte — e la riga `--- tooltip: riga 1 di 1 voci` ha spiegato da
sola la causa. Un `printf` in più costa un giro; dieci minuti di ragionamento su
una caduta che non si riproduce costano un pomeriggio.

**Un registro aperto in append a ogni riga può perdere le righe.** Il primo
diagnostico dei verbi della shell apriva, scriveva una riga e chiudeva; il file
risultante conteneva **118 byte su venti voci** — il riassunto e un frammento di
due voci, il resto sparito. Il secondo giro dello stesso problema non costa nulla
se le righe si accumulano e si riscrivono tutte insieme: un registro incompleto è
peggio di nessun registro, perché sembra dire che quelle voci non esistono.

**Un menu più alto della finestra non è un dettaglio estetico.** Le voci che
sporgono sotto il bordo non si leggono e non si cliccano, e il menu contestuale
con i verbi della shell è passato da undici a trentuno voci. Un menu che scorre
va previsto nel componente, non aggiunto quando serve: la correzione è costata
una riscrittura della geometria (`firstItem`, altezza derivata dalle voci che
entrano, barra sul bordo destro). Vedi §7.16 di `docs/02`.

**Con più di una modalità di scorrimento, le regole sono più di una.** Le frecce
devono portarsi dietro la voce scelta; la rotella no. Usare una regola sola per
entrambe ha prodotto un menu che si rifiutava di scorrere sotto il puntatore. E
una prova che non distingue il caso rotto dal caso giusto non è una prova: il
menu straripava di **una** voce, quindi la rotella toccava il tetto comunque. La
finestra è stata ridotta a 460 px per rendere la misura decidibile.

**Scegliere una voce col puntatore è un bersaglio mobile.** Il menu contestuale
non ha la stessa altezza a ogni apertura: dalla seconda in poi arrivano le
estensioni moderne di Windows 11, quindi le voci sotto il puntatore scendono di
due righe. Un clic a coordinate fisse ha sbagliato voce, e la cattura sembrava
dire che il comando non era partito. Per scegliere una voce precisa si apre il
menu e si usa la **tastiera** (`Fine`, poi tante `giù` quante servono): è
deterministico e non dipende dalla geometria.

**Una sonda che cerca per titolo trova anche la finestra dell'applicazione.**
Il titolo di Bolt contiene il percorso della cartella aperta, quindi la ricerca
di `sandbox` ha trovato due finestre — la nostra e il dialogo delle proprietà — e
il `WM_CLOSE` di pulizia le ha chiuse **entrambe**: l'applicazione è morta
durante una prova, e per un momento è sembrato che fosse il dialogo a
ucciderla. `tools/find-window.ps1` ora salta la classe `BoltWindow`.

**`GetWindowTextW` con `CharSet` sbagliato tronca i titoli alla prima lettera.**
Una enumerazione di finestre restituiva `B`, `N`, `_`, `M`, `D`: sembrava che il
processo avesse sette finestre senza nome, e invece il marshalling ANSI leggeva a
sedici bit una stringa scritta a sedici bit. Il difetto era nello strumento, come
il §7.14: prima di dichiarare che una finestra non esiste, si verifica che la
sonda la sappia leggere.

**`IFileOperation` non ha un metodo per fermarsi.** Non esiste `Abort`: chi annulla e'
il **sink**, che risponde con un codice d'errore. Il documento dice che un errore da
`Pre*Item` annulla l'operazione e **tutte quelle in coda**, e `UpdateProgress` passa
di li' anche a meta' di un file solo. Il primo passo di M7, scritto in §3 di questo
documento, diceva `IFileOperation::Abort`: e' stato il **compilatore** a dire che quel
metodo non c'e', dopo che la riga era stata scritta in un documento di progetto e
riletta come se fosse vera. Un documento di progetto e' una fonte come il grafo: una
derivazione, non la verita'.

**I "punti" di `UpdateProgress` non sono byte.** Misurati: un file da 4 GB sono
**131074 punti** (32 KB l'uno), un file da 23 byte sono **2 punti**. La frazione e'
una frazione e vale come stima del tempo; per avere dei **byte** serve un denominatore
misurato da noi. Un conto che sembra esatto e non lo e' e' peggio di un conto
dichiarato approssimativo, e il modo di accorgersene e' una riga di registro che scrive
le due cose **nella stessa riga**: `operazione: 4294967296 byte misurati, 1 voci,
131074 punti dichiarati dalla shell, 69760 fatti`.

**Un'operazione lunga su un thread di lavoro rende possibile la seconda operazione.**
Sostituire quella in corso vuol dire annullarla **senza dirlo**, e per una caduta e'
peggio che per un comando: chi ha mollato il tasto legge dalla destinazione se
l'operazione e' riuscita, e su `DROPEFFECT_MOVE` cancella gli originali. Una caduta
rifiutata deve dichiarare `DROPEFFECT_NONE`, o il rifiuto diventa una perdita di dati.

**Due catture si confrontano solo a parita' di stato — e l'ispettore ha una riga che
cambia da sola.** Per verificare lo spostamento dell'ispettore ho confrontato le
stesse cinque catture prima e dopo: 72 pixel diversi in ognuna, tutti nella riga
**Accesso**. Non era il refactor: era la **data di ultimo accesso** della cartella,
cambiata fra i due giri perche' nel frattempo qualcuno l'aveva letta (12:39 → 15:30).
Lo stesso binario, due giri di fila, da' catture **identiche** tranne il pixel di
vetro del bordo. Morale doppia: un confronto a pixel va fatto **nello stesso giro**
quando c'e' di mezzo un tempo, e una cattura che differisce in una riga di data non
dice niente sul codice.

**Un taglio con una graffa in meno non da' "graffa mancante": da' "chiamata ambigua
a funzione in overload".** Spostando i menu, la cucitura fra la prima funzione e il
resto ha perso una graffa: la funzione e la graffa che chiudeva il namespace anonimo si
sono scambiate i ruoli, e **tutto il resto del file e' finito dentro il namespace
anonimo**. Il compilatore l'ha detto cosi': `'Bolt::anonymous-namespace'::GroupMenuMove':
chiamata ambigua a funzione in overload`, con due candidati identici. Un errore che nomina
l'overload quando il problema e' una graffa si risolve leggendo la **cucitura**, non la
funzione che l'errore nomina. E vale la regola di sempre: dopo un taglio si legge cio'
che si e' tolto e cosa sta **attorno** alla cucitura.

**La finestra si sposta a ogni avvio, e una prova con l'origine sbagliata non
fallisce: misura un'altra cosa.** `send-keys.ps1` e `send-mouse.ps1` vogliono
coordinate **cliente** e le convertono da soli, ma `capture-rect.ps1` e
`drag-mouse.ps1` vogliono coordinate **schermo**, e l'origine va **rimisurata dopo
ogni avvio** (`window-rect.ps1`): `CW_USEDEFAULT` la sposta. In questa sessione due
catture sono uscite sul riquadro sbagliato — la barra delle schede invece della lista
— perche' l'origine era quella dell'istanza precedente. Una cattura sbagliata sembra
una prova, ed e' per questo che fa perdere piu' tempo di un errore.

**Due istanze di Bolt, e i tasti vanno a una sola.** `send-keys.ps1` e
`send-mouse.ps1` prendono il processo con `Get-Process Bolt | Select-Object -First 1`:
con due istanze vive — l'istanza vecchia ancora in chiusura e la nuova appena partita —
meta' dei tasti finisce in una finestra e meta' nell'altra, e il risultato sembra
un'applicazione che ignora i tasti a caso. Prima di una prova con l'input: si uccide
il processo e si **aspetta che non ci sia piu'** (`while (Get-Process Bolt) { Start-Sleep
-200 }`), poi si avvia. E' successo mentre si costruiva il contenuto della cartella nell'ispettore: tre `giu'` di fila
hanno spostato il cursore di **una** riga, e la cattura sembrava dire che il pannello
non si aggiornava.

**`small` e' una macro di `rpcndr.h`.** `const Rect small{...}` non compila, e l'errore
dice `'Rect' non puo' essere seguito da 'char'` — nomina il tipo, **non** la macro, e
sembra un errore di sintassi. Vale per gli identificatori che il C aveva gia' preso
(`small`, `near`, `far`): un errore che nomina il tipo accanto a un identificatore
innocuo si risolve guardando l'identificatore, non il tipo.

---

## 6. Convenzioni del progetto

- Commenti e documentazione in **italiano**; identificatori e stringhe di codice
  in inglese.
- Ogni funzione nuova che risolve un problema non ovvio porta un commento che dice
  **perché**, non cosa. Il codice dice cosa.
- I numeri **derivati** battono i numeri fissati a occhio (vedi §7.6 di `docs/02`).
- Ciò che non è verificato a schermo o contro disco non è "fatto": è "scritto".
- I limiti si dichiarano nell'interfaccia o nella roadmap, non si nascondono.
- Build **senza warning** (`/W4`): un warning nuovo è un difetto nuovo.
- **Il banco di prova si verifica quanto il prodotto.** Un difetto visibile a
  chiunque usi il programma e invisibile allo strumento che lo controlla non è un
  difetto sfuggito: è uno **strumento sbagliato**. La cattura parte dalla finestra
  intera, non dall'area cliente.
- Quando un valore è in dubbio, **scriverlo in un log e leggerlo** batte dedurlo.
