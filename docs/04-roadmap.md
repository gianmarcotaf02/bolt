# Roadmap

Sequenza di lavoro, ordinata in modo che ogni milestone produca qualcosa di
funzionante e verificabile. Le stime sono per lavoro part-time.

Stato corrente: **M1, M2, M2b, M3 (nucleo), M5 e M5c (in gran parte) completate;
M6 a metà.** Della 2 resta la resa monocromatica delle icone, della 3
l'invalidazione da notifiche del file system e i byte allocati, della 6 GoTo fatto
e restano quick access pinnato, drag&drop e albero dei tab, della 5c il secondo
corpo di glifi e il raggruppamento in griglia.

---

## M1 — Shell e architettura di rendering ✅

Obiettivo: dimostrare end-to-end che l'architettura ricostruita funziona.

- [x] finestra Win32, DPI per-monitor
- [x] contesto OpenGL 3.3 core con fallback
- [x] loader runtime delle entry point (nessuna dipendenza esterna)
- [x] quad batcher: 4 vertici da 30 float per quad, indice condiviso, una draw call
- [x] shader SDF originali: riempimenti arrotondati, bordi, ombre, dash, gradienti
- [x] texture array per le icone, con maschera monocromatica
- [x] atlas di glifi via GDI
- [x] enumerazione directory reale
- [x] shell a tre regioni con scorrimento, selezione, zoom

**Verifica:** l'eseguibile si avvia, le shader compilano, la UI disegna ed è
navigabile. Il numero di quad al frame è visibile nella barra di stato.

---

## M2 — Icone e anteprime reali 🟡

L'atlas delle icone di sistema e quello delle anteprime sono fatti; resta solo il
percorso monocromatico.

- [x] `SHGetFileInfoW` con `SHGFI_USEFILEATTRIBUTES`: icone per estensione e per
      attributi, senza toccare il disco
- [x] cache per chiave (`<dir>`, `<file>`, `.estensione`) in una griglia
      dell'atlas
- [x] conversione da HICON a RGBA, con alpha premoltiplicato rimosso e fallback
      alla maschera AND per le icone senza canale alpha
- [x] `IShellItemImageFactory` per anteprime su misura: `SIIGBF_THUMBNAILONLY`,
      così un tipo senza provider fallisce e ricade sull'icona invece di
      restituire l'icona stessa spacciata per anteprima
- [x] dimensione dell'atlas limitata da `AssetCacheSize` ed eventuale sfratto:
      atlas di 64 celle da 128 px e politica least-recently-used con riuso della
      cella
- [ ] resa monocromatica delle icone in tema scuro (percorso `textureIsMono`)

*Nota sull'ultima voce.* Il percorso `textureIsMono` esiste nella shader ed è
pronto, ma non è ancora usato: le icone di sistema sono a colori (cartelle
gialle, note musicali rosa) e applicarlo a quelle le appiattirebbe. Serve un
caso d'uso vero — un glifo monocromatico da tingere — prima di accenderlo,
altrimenti è solo un flag che nasconde un errore.

*Il nucleo è fatto: icone reali per estensione, anteprime reali per immagini e
video, e la memoria dell'atlas resta limitata mentre si scorre una cartella
grande.*

**Design delle anteprime.** La decodifica gira su un worker thread con
apartment MTA, perché un JPEG freddo su una condivisione di rete costa decine di
millisecondi e bloccherebbe il frame loop. Il worker produce RGBA ad alpha
non premoltiplicato in una cella; l'upload su texture avviene sul thread che
possiede il contesto GL, in `Pump()`, una volta per frame. Le richieste sono
deduplicate mentre sono in volo e azzerate al cambio di cartella, così uscire da
una cartella da 1500 file non lascia il worker a decodificare anteprime che non
sono più a schermo. Vedi `src/shell_preview.h`.

---

## M2b — Navigazione e usabilità ✅

Nato come contorno della milestone 2, ma è ciò che rende il programma usabile.

- [x] doppio click per entrare in una cartella, o aprire il file con l'app
      associata
- [x] `Backspace` risale al genitore, `F5` ricarica, `Invio` apre
- [x] breadcrumb cliccabile componente per componente
- [x] intestazioni di colonna cliccabili, con inversione al secondo click e
      indicatore di ordinamento
- [x] troncamento con ellissi del testo che eccede la colonna
- [x] `Size` allineata a destra
- [x] quick access dalle cartelle note reali (`SHGetKnownFolderPath`)
- [x] cartella corrente evidenziata nella sidebar
- [x] percorso di partenza da riga di comando
- [x] DPI per-monitor come scala iniziale della UI
- [x] log degli errori di avvio su file, non solo MessageBox

---

## M3 — Statistiche di cartella 🟡 ◀ **il pezzo che distingue il prodotto**

> **Correzione.** Questa milestone era intitolata "Statistiche da MFT" e
> prevedeva la lettura e l'aggregazione del Master File Table. Quella ipotesi è
> stata **falsificata**: la firma dei record MFT non compare nel codice e le
> stringhe `NTFS`/`$MFT`/`USN` non esistono nel binario (vedi `docs/01`). Il
> lavoro è quindi di natura diversa e non richiede privilegi elevati.

Modello corretto: enumerazione parallela ottimizzata, dentro un job con budget di
tempo, con risultati memorizzati e invalidati.

- [x] job `SumDirStats` nella work queue, con avanzamento parziale pubblicato
- [x] enumerazione con `FindFirstFileExW` in modalità `FindExInfoBasic` +
      `FIND_FIRST_EX_LARGE_FETCH`, e fallback a `FindFirstFileW` dove non ammessa
- [x] accumulo di byte logici e conteggi di file e cartelle
- [ ] accumulo dei byte **allocati** (serve `GetCompressedFileSizeW` o
      `FILE_ID_BOTH_DIR_INFO`: l'enumerazione base non li espone)
- [x] tre profili di concorrenza e budget per SSD, HDD e rete
- [x] budget di tempo per ciclo, interrompibile e riprendibile
- [x] cache dei risultati con distinzione fra parziale e completo
- [ ] invalidazione da `ReadDirectoryChangesW` (per ora si invalida con `F5`)
- [x] attivazione su richiesta: solo le righe visibili e la selezione
- [x] reparse point non seguiti; hard link contati per nome (vedi nota)
- [x] degradazione su volumi non NTFS e su rete

### Come è implementato — *Ricostruito*

`src/dir_stats.{h,cpp}`. Il modello segue la macchina ricostruita: un job per
cartella, budget di tempo per fetta, risultati parziali pubblicati mentre il job
gira, e un token per cartella che rende **stale** ogni job in volo quando la
cartella viene invalidata — la stessa idea del confronto di generazione
`DAT_140249268` del binario di riferimento.

La differenza sostanziale rispetto all'originale è dove.

**Profili di volume.** La concorrenza e la fetta di tempo dipendono dal
dispositivo: 3 worker e 8 ms su SSD, 1 worker e 25 ms su disco meccanico, 2
worker e 40 ms su rete. Il tipo si ricava da `GetDriveTypeW` e, per i dischi
fissi, dalla proprietà `StorageDeviceSeekPenaltyProperty` interrogata con
`DeviceIoControl` — che è anche l'unico import di `DeviceIoControl` presente nel
binario di riferimento, quindi l'ipotesi più semplice che lo giustifica.

> **Attenzione:** questa è una **ricostruzione plausibile**, non un reperto. La
domanda n. 3 del registro («quale parametro differenzia SSD, HDD e rete») resta
aperta finché non si localizza chi referenzia `Setting_CalcFolderSizesSSD`.

**Concorrenza e ripresa.** Le fette scadono a granularità di *cartella*: non si
può sospendere `FindNextFileW` a metà di una directory e riprenderla, quindi il
budget si controlla fra una directory e la successiva, e ogni 4096 voci si
pubblica comunque un parziale, per non lasciare muta una directory enorme.

**Reparse point e hard link.** I reparse point non vengono seguiti: sono link,
seguirli conterebbe due volte la destinazione e un ciclo non terminerebbe. Gli
hard link restano contati una volta per nome, perché distinguerli richiede un
identificatore di file per voce e l'enumerazione base non lo fornisce. È un
limite dichiarato, non un errore nascosto.

**Domande per REA, in ordine di priorità:**

1. Nella funzione che referenzia `Dir_SumDirStatsWorkUpdate`, quale granularità ha
   l'aggiornamento parziale e dove viene controllato il budget di tempo?
2. Nella funzione che referenzia `Setting_CalcFolderSizesSSD`, quale parametro
   cambia rispetto a HDD e rete: concorrenza, dimensione della fetch, o soglia di
   interruzione? I tre valori hanno percorsi di codice distinti o differiscono in
   una tabella di costanti?
3. Come vengono evitati i doppi conteggi su reparse point e hard link?
4. La cache dei risultati sopravvive fra sessioni o è solo in memoria?

**Come trovare le funzioni.** La tabella dei nomi dei job a `0x140247ed0` non ha
riferimenti statici, quindi la ricerca va fatta **dal lato codice**: cercare le
funzioni che chiamano `FindFirstFileExW` sono poche decine in tutto il binario, e
fra queste va cercata quella che accumula conteggi e byte. Questo è il primo
compito della sessione di reverse successiva.

*Stima: 2–4 settimane.* Meno della stima precedente, perché non servono
privilegi elevati né la correttezza di un formato su disco non documentato; resta
il lavoro sui casi limite (reparse point, hard link, errori di I/O parziali).

---

## M3b — Attributi NTFS di un file ⬜

Feature separata e più piccola, dedotta dalla tabella `AT_*` e dagli import
`CreateFileW` + `DeviceIoControl`.

- [ ] apertura del volume raw `\\.\X:`
- [ ] `FSCTL_GET_NTFS_FILE_RECORD` per il record di un file identificato
- [ ] mappatura dei codici di attributo ai nomi della tabella `AT_*`
- [ ] presentazione nell'inspector
- [ ] degradazione pulita senza privilegi elevati

*Stima: 3–5 giorni.* È un percorso isolato e ben delimitato.

---

## M4 — Ranking dei recenti ⬜

- [ ] ricostruire la formula da `Score`, `Decay`, `Ordinal`
- [ ] distinguere i tre modi di `StoreRecents`: `LocalDisk`, `Persistent`, `Session`
- [ ] integrare con la finestra GoTo e i gruppi di accesso rapido

**Domanda per REA:** nella funzione che scrive `"Score": %d,` e `"Decay": %d,`,
quali sono le grandezze di ingresso e l'ordine degli aggiornamenti quando una voce
viene riusata?

*Stima: 1 settimana.* REA è decisiva qui: è un algoritmo isolato e piccolo, il
caso ideale.

---

## M5 — Ricerca, filtro, ordinamento, raggruppamento ✅

- [x] filtro incrementale sulla lista (`Panel_FilterHint`, `FilterText`, `FilterSticky`)
- [x] filtro per tipo di file (`FilterFileTypes`, `FilterTypes`)
- [x] ordinamento per ogni colonna (`SortedBy`, `SortIndex`, `FolderSort`)
- [x] raggruppamento sulle cinque dimensioni di `GroupByLabel_*`
- [x] righe di intestazione di gruppo collassabili
- [x] selezione multipla e rettangolare (`Color_RectSelection`)

### Il modello di vista — *Ricostruito*

Con filtro e raggruppamento la lista **non è più la directory**: una riga non
corrisponde più a una voce. `AppState::rows` è una lista di righe sopra l'array
delle voci, con `Row::header` che distingue una banda di gruppo da una voce, e
viene ricostruita solo quando cambia qualcosa che la riguarda: filtro,
raggruppamento, ordinamento, cartella, oppure un risultato di statistica nuovo e
solo ogni 250 ms, perché altrimenti un publish per fetta diventerebbe un rebuild
per frame.

La selezione resta un indice nella directory, non nella vista: l'inspector e
l'apertura non devono sapere che esiste un filtro. Dove invece la vista conta è
navigazione e disegno, quindi `MoveSelection` salta le bande,
`EnsureSelectionVisible` riporta in vista la riga corrispondente e lo scorrimento
conta le righe.

**Filtro.** Termini separati da spazi, in AND. Un termine che inizia con un punto
o contiene un jolly è una richiesta sul tipo (`.pdf`, `*.png`) invece che sul
nome: è il percorso `FilterFileTypes`/`FilterTypes` espresso nella stessa casella
del filtro testuale. `FilterSticky` esiste nella tabella delle chiavi; qui il
filtro **non** è sticky, perché un filtro ereditato da un'altra cartella sembra
una cartella vuota.

**Raggruppamento.** Cinque dimensioni, con le etichette prese dalla tabella
`GroupByLabel_*`. Le **soglie** non lo sono: `docs/03` §8 le registra come ignote,
quindi in `GroupKeyFor` sono valori scelti e isolati in un unico punto, facili da
correggere quando l'osservazione del target li rivelerà. La dimensione
sull'occupazione del volume usa `GetDiskFreeSpaceExW`, come l'originale.

**Conteggio delle corrispondenze.** La barra del filtro mostra
`corrispondenze / totale`. Conta le voci che superano il filtro, non le righe
disegnate: collassare un gruppo non deve sembrare un cambiamento del filtro.

### Selezione — *Ricostruito*

**Per nome, non per indice.** Ordinare, filtrare e ricaricare rinumera l'array
delle voci: una selezione tenuta per indice scivola su un file diverso appena
l'utente ordina per dimensione — un difetto peggiore di nessuna selezione. Quindi
la selezione è un insieme di **nomi**, e l'indice si ritrova quando serve.

Le tre modalità dello standard Windows, senza inventarne di nuove:

| gesto | effetto |
|---|---|
| click | seleziona solo quella voce, e la rende cursore e ancora |
| `Ctrl`+click | aggiunge o toglie la voce |
| `Shift`+click, `Shift`+frecce | intervallo dall'ancora al cursore, ricalcolato a ogni passo (così tornando indietro si restringe) |
| trascinamento su area vuota | banda rettangolare |
| `Ctrl` o `Shift` + trascinamento | la banda si somma alla selezione |
| `Ctrl+A` | tutto ciò che è a schermo: il filtro fa parte della domanda, quindi fa parte della risposta |

**Un difetto trovato verificando.** La banda teneva conto solo di `Ctrl`, non di
`Shift`, quindi `Shift`+trascinamento **cancellava** la selezione invece di
sommarsi. È emerso perché un click di prova cadeva a un pixel dal bordo di una
riga: il click mancava la riga, finiva nel ramo "area vuota" e azzerava tutto.
Il difetto era reale, il test era sbagliato per un pixel — ed è servito lo stesso.

**Riempimento e contorno sono separati**, come i token `ContentSelection` e
`OutlineSelection` dell'originale: con più righe selezionate bisogna comunque
vedere su quale sta lavorando la tastiera.

La banda si disegna sopra le righe che sta per selezionare e solo dentro la
regione della lista, così trascinare oltre il bordo non sembra selezionare la
sidebar. L'hit test alla fine del trascinamento usa i rettangoli delle righe
registrati dal frame precedente: al più un frame di ritardo, che conta solo se
l'utente rilascia nell'istante in cui la lista scorre.

**Limite dichiarato.** La somma delle dimensioni nell'inspector di una selezione
multipla usa le statistiche **già in cache** per le cartelle: chiedere il conteggio
di una cartella fuori schermo avvierebbe lavoro che l'utente non vede.

---

## M5c — Modalità di visualizzazione 🟡

Due impaginazioni (righe di testo, griglia di celle) e due dimensioni di cella
coprono le otto voci di visualizzazione di Explorer, perché la maggior parte di
quelle sono la stessa impaginazione a una scala diversa.

- [x] `Details` — righe con tutte le colonne, intestazione visibile
- [x] `List` — righe compatte, icona più piccola, stesse colonne
- [x] `Icons` — griglia, cella da 64 px di miniatura
- [x] `Tiles` — griglia, cella da 128 px di miniatura
- [x] nome su due righe con ellissi nell'ultima, centrato sotto la miniatura
- [x] intestazione nascosta in griglia, spazio restituito alla lista
- [x] frecce: `Su`/`Giù` di una riga di griglia, `Sinistra`/`Destra` di una cella
- [x] `Ctrl`+`1`/`2`/`3`/`4`, più i quattro comandi nella palette
- [x] secondo corpo di glifi a 13 px per il testo secondario (vedi §7.12 di
      `docs/02`), su un quarto layer dell'array di texture
- [ ] raggruppamento in griglia: richiede un impaginatore a flusso con bande a
      piena larghezza; al momento la griglia lo disattiva e lo dichiara
- [ ] ridimensionamento continuo della cella (la scala fra `Icons` e `Tiles`)

### Perché le celle sono alte quanto dicono di essere — *Ricostruito*

L'altezza della cella era un numero fisso scelto a occhio, e l'etichetta a due
righe finiva **sopra** la miniatura: si vedeva il nome stampato sulla foto. Ora
l'altezza si **deriva** da icona più etichetta, e nel disegno la dimensione
dell'icona viene comunque ridotta se lo spazio sopra l'etichetta non basta. Un
numero derivato non può entrare in disaccordo con le sue parti.

### Cosa manca di questa passata

Il **raggruppamento in griglia**, che richiede un impaginatore a flusso, e il
**ridimensionamento continuo** della cella. Il secondo corpo di glifi è fatto.

---

## M5d — Sovrappasso e comportamento del clic 🟡

Non era nei piani. È emerso da una domanda dell'utente — «quando passo sopra dei
file si preseleziona qualcosa?» — e la risposta era che **no, e non era previsto**:
la roadmap non ne parlava, ma lo spec funzionale dell'originale sì. Lacuna mia, non
una scelta.

Il campo `app.hovered` era già calcolato a ogni `WM_MOUSEMOVE` e **non veniva mai
letto**: l'impianto c'era, il disegno no. Il token di tema `Hover` / `ContentHover`
esisteva già fra i 35.

- [x] riquadro di sovrappasso su righe e celle di griglia
- [x] si spegne uscendo dalla finestra (`TrackMouseEvent` + `WM_MOUSELEAVE`) —
      senza, resterebbe appeso sull'ultima riga toccata, perché uscendo non arriva
      più nessun `WM_MOUSEMOVE`
- [x] la selezione vince sul sovrappasso: la riga selezionata **non** cambia colore
      sotto il puntatore
- [ ] `HoverToSelect` — il passaggio **seleziona**, non solo evidenzia
- [ ] `UnderlineOnHover` — sottolineatura al passaggio
- [ ] `ShowTooltips`
- [ ] `ActOnPress`, `SingleClick`, `SingleClickFoldersOnly`, `DoubleClickToParent`,
      `ShowCheckboxes`

### Cosa fa l'originale, e cosa non abbiamo ancora — *Ricostruito*

Lo spec funzionale elenca otto chiavi di interazione (`docs/03`, sezione
Interazione), e il tema ha **quattro** token di sovrappasso: `Hover`,
`ContentHover`, `MatchHover`, `OutlineHover`. La famiglia `Match*` copre quattro
stati di una riga che corrisponde al filtro — normale, sovrappasso, selezionata,
selezionata ma disabilitata — quindi nell'originale il sovrappasso **convive con
la selezione** invece di essere assorbito da essa.

Nota di merito: `HoverToSelect` è una **chiave di configurazione**, quindi
nell'originale non è il comportamento predefinito. Evidenziare e selezionare sono
due cose distinte, e la seconda si accende. Anche `UnderlineOnHover` è una chiave,
non un dato di fatto.

---

## M6 — Tab, quick access, palette, GoTo 🟡

- [x] tab multipli: creazione, chiusura, attivazione, stato indipendente
- [x] barra del titolo propria al posto di quella nativa, con pulsanti di finestra
- [x] command palette con gruppi collassabili
- [x] finestra GoTo con recenti e segnalibri
- [x] componente menu a gruppi, condiviso da palette e GoTo
- [x] cronologia di navigazione per tab (`Alt`+`←` / `Alt`+`→`)
- [ ] drag&drop dei tab (`TabDropMarker_*`)
- [ ] albero dei tab (`CloseInTree`, `CloseAllInTree`)
- [ ] quick access con percorsi pinnati
- [ ] il terzo contesto del componente a gruppi: il menu contestuale (arriva con M7)

### Il componente a gruppi, e quando è stato estratto — *Ricostruito*

`GroupMenu` è una lista modale, filtrabile, raggruppata e collassabile che **non
sa cosa significhi una voce**: `id` è affare di chi la chiama. È ciò che le
permette di servire sia la palette dei comandi sia la finestra GoTo.

**È stata estratta quando è arrivato il secondo utilizzatore, non prima.** Al
momento della palette l'astrazione sarebbe stata indovinata: non si sapeva quali
parti sarebbero state comuni — filtro, gruppi, navigazione, disegno della banda —
e quali specifiche del contesto. Con GoTo la cucitura è diventata evidente, e
l'estrazione è stata uno spostamento. Il terzo contesto previsto dalla roadmap è
il menu contestuale della shell, che arriva con le operazioni su file.

**Cronologia, recenti e segnalibri.** La cronologia è **per tab** e si salva e
ricarica insieme al resto dello stato del tab; camminare verso una destinazione
nuova scarta il ramo in avanti, come in ogni browser. I recenti e i segnalibri
sono **globali e di sessione**: `StoreRecents` ha tre modalità nella tabella
delle chiavi (`LocalDisk`, `Persistent`, `Session`) e questa è la terza, quindi
nulla viene ancora scritto su disco — la persistenza è M9.

**`Alt`+`←`/`→` arrivano come `WM_SYSKEYDOWN`, non `WM_KEYDOWN`,** perché la
finestra ha un menu di sistema: vanno intercettati lì o non arrivano affatto. Il
raggruppamento si è spostato su `Ctrl`+`Shift`+`G` per lasciare a GoTo la
combinazione ovvia.

### Command palette — *Ricostruito*

Un comando è **dato, non callback**: la tabella `kCommandTable` è l'unico punto in
cui un comando viene dichiarato, `RunCommand` l'unico in cui viene eseguito.
Dichiarare ed eseguire nello stesso posto è comodo finché i comandi sono trenta e
diventa un problema quando sono trecento; la separazione è ciò che rende
sostituibile la tabella con una generata.

Le voci sono raggruppate e i gruppi si collassano, come nell'originale. Solo i
gruppi che la shell sa davvero eseguire sono presenti: `Tabs`, `Folder`, `View`,
`Sort`, `GroupBy`, `Text`, `Scroll`, più `Places`, che è generato dai percorsi
noti invece di avere un comando per ciascuno. Niente voci morte.

**Perché il componente riusabile non c'è ancora.** La roadmap lo prevede condiviso
fra palette, menu contestuale e GoTo. Estrarlo adesso significherebbe indovinare
la cucitura con un solo utilizzatore: si estrae quando il secondo rivela quali
parti sono davvero comuni — quali fra filtro, gruppi collassabili, navigazione e
disegno della banda — e quali sono specifiche del contesto. La palette è quindi
scritta in modo che l'estrazione sia un spostamento, non una riscrittura.

**Ereditarietà della palette.** Non eredita il filtro della lista: apre con la
propria casella vuota. Il filtro della lista risponde a "quali file", quello
della palette a "quale comando": sono due domande diverse e condividerne una
sarebbe un errore.

### Barra del titolo propria — *Ricostruito*

Il titolo nativo viene rimosso rispondendo a `WM_NCCALCSIZE` con zero: il client
copre l'intera finestra e il disegno della barra passa all'applicazione, che è il
presupposto dei tab. `WM_NCHITTEST` restituisce `HTCLIENT` su pulsanti e tab (che
devono restare cliccabili), `HTCAPTION` sul resto della striscia — il che regala
trascinamento, doppio click per massimizzare e snap senza scriverli — e i codici
di bordo per il ridimensionamento, che restano attivi solo quando la finestra non
è massimizzata.

**Il caso massimizzato è quello che si sbaglia.** Una finestra massimizzata è
gonfiata dal bordo di ridimensionamento: senza correzione il contenuto finisce
sotto il bordo dello schermo. `WM_NCCALCSIZE` rientra quindi il client della
dimensione del frame quando `IsZoomed` è vero.

**Stato per tab.** Il tab conserva cartella, voci, selezione, cursore,
scorrimento, filtro, ordinamento e raggruppamento. Il passaggio di tab salva lo
stato vivo nel tab da cui si esce e carica quello del tab in arrivo: il resto del
programma continua a lavorare su un solo insieme di campi e non ha mai bisogno di
sapere che i tab esistono. Le cache di anteprime e statistiche sono globali e
indicizzate per percorso, quindi sopravvivono al cambio senza lavoro.

*Nota:* il titolo del tab attivo viene dallo stato vivo, non dallo snapshot — lo
snapshot è vuoto mentre il tab è attivo, perché le sue voci sono state spostate
fuori. È un dettaglio che si vede subito: il tab dell'appena avviato programma
mostrava "This PC" al posto della cartella.

*Stima: 3–5 settimane.* È qui che va speso il tempo per unificare il componente
a gruppi, invece di scriverne tre.

---

## M7 — Operazioni su file 🟡

- [x] copia, sposta, rinomina, crea cartella (`IFileOperation`)
- [x] eliminazione verso il cestino reale, e definitiva con `Shift`+`Canc`
- [x] clipboard con formato shell (`CF_HDROP` + effetto preferito), quindi
      interoperabile con Esplora risorse nei due sensi
- [x] rinomina sul posto, con il campo disegnato sopra la riga
- [x] esito delle operazioni nella barra di stato
- [x] scorciatoie e comandi nella palette
- [x] dopo un incolla con conflitto, la selezione cade sull'elemento che la shell
      ha davvero creato, non su quello di partenza
- [x] le statistiche della cartella vengono invalidate dopo un'operazione
- [x] menu contestuale del tasto destro: si ancora al puntatore, alto quanto il
      contenuto, con separatori e navigazione da tastiera; la riga sotto il
      puntatore viene preselezionata
- [x] Proprietà, che apre la finestra vera di Windows via `ShellExecuteEx`
- [ ] ripristino dal cestino — **bloccato da una scelta strutturale**, vedi sotto
- [ ] drag&drop in ingresso e in uscita con descrizione
- [x] le voci di **primo livello** della shell dentro il menu contestuale (Apri con,
      le voci delle estensioni installate), dopo i nostri comandi e dopo un
      separatore. La strada è quella del binario originale —
      `IShellFolder::GetUIObjectOf` → `IContextMenu` → `QueryContextMenu` →
      `GetCommandString(GCS_VERBW)` → `InvokeCommand`: vedi sotto
- [ ] i **sottomenu** del menu della shell: `Invia a`, `7-Zip`, `WinRAR`,
      "Dare accesso a", "Includi nella raccolta" restano fuori, e sono quattro o
      cinque voci su venti. È il pezzo che manca perché il menu coincida con
      quello di Esplora risorse
- [ ] il menu della **cartella** (niente selezionato): nel vuoto le voci della
      shell non si chiedono affatto, quindi `Nuovo >` e i verbi del contesto
      cartella non ci sono
- [ ] dialogo di progressione con annullamento proprio

### Il menu contestuale non è un `GroupMenu` — *Scelta*

Condividono l'idea di "una lista di righe etichettate", ma differiscono in tutto
il resto: il menu contestuale si ancora al puntatore invece di stare al centro,
è alto quanto il contenuto invece di 420 px fissi, ha **separatori** e non bande,
e non ha né filtro né piede. Forzarlo dentro `GroupMenu` avrebbe voluto dire un
campo "modalità" e due impaginazioni dentro lo stesso componente.

Il menu **non aggiunge capacità**: riusa i comandi che già esistono e li rende
raggiungibili dove servono.

### I verbi della shell nel menu contestuale — *Ricostruito*

La shell sa fare cose che non sono nostre: `Apri con`, `Invia a`, i verbi delle
estensioni installate (`Converti a Adobe PDF`, `Modifica nel Blocco note`,
`Ripristina versioni precedenti`, `Copia come percorso`). Non si reimplementa
niente. Il binario originale fa la stessa cosa — `Win_GetIShellFolder`,
`Win_GetUIObjectOfPaths`, `P_GetContextMenu`, `Win_ContextMenuBuild`,
`P_ContextMenuInvoke`, e la stringa `GCS_VERBW` — quindi la strada è quella:

```
SHParseDisplayName → SHBindToParent(IID_IShellFolder) → GetUIObjectOf(IID_IContextMenu)
   → QueryContextMenu in un HMENU che non mostriamo mai → GetMenuItemInfoW / GetCommandString
   → InvokeCommand(CMINVOKECOMMANDINFOEX con lpVerbW = MAKEINTRESOURCE(id - kShellCmdFirst))
```

**L'HMENU non si mostra.** Chi disegna resta il nostro renderer, quindi il menu
resta coerente con il resto della finestra; l'HMENU serve solo a leggere le voci,
un po' come il PDB serviva solo a leggere i nomi. Vive quanto l'`IContextMenu` e i
due si rilasciano insieme alla chiusura: verificato contando gli **oggetti USER**
del processo, che non crescono (41 → 40 su cinque aperture), mentre se l'HMENU
restasse appeso crescerebbe di uno per tasto destro.

**Gli id della shell vivono in una banda riservata** (`0x6000`–`0x6FFF`): dentro
quella banda un id è un indice per `IContextMenu`, fuori è un nostro `CommandId`.
Così una voce sola può essere eseguita da chi la sa eseguire, senza un campo in
più che dica di chi è.

**Le voci che offriamo già si tolgono per verbo canonico, non per etichetta**:
`open`, `cut`, `copy`, `paste`, `delete`, `rename`, `properties`. L'etichetta è
tradotta e cambia con la lingua di Windows, il verbo no. Senza questo filtro il
menu avrebbe due `Taglia` e tre `Proprietà`. `openas` **non** è nell'elenco: è
proprio uno dei motivi per cui la shell viene interrogata.

**Le voci che la shell offre spente si mostrano spente.** Una selezione mista non
è una ragione per nascondere che quel verbo esiste: le frecce le saltano e l'Invio
non le esegue, ma la voce resta leggibile.

**Quattro costi misurati** (registro: `BOLT_DIAG_SHELL_MENU=1` → `shell-menu.log`):

| misura | valore | lettura |
|---|---|---|
| prima apertura dopo l'avvio | ~350 ms | le DLL delle estensioni si caricano in-process |
| regime | ~65 ms | il costo di ~15 gestori interrogati a ogni tasto destro |
| handle di kernel | +22 per apertura | dentro le estensioni: i nostri oggetti USER non crescono |
| estensioni moderne Win11 | dalla **seconda** apertura | `windows.storage` registra le proprie voci al secondo giro |

Gli ultimi due sono limiti dichiarati, non risolti: il primo non dipende da noi,
il secondo è una scelta da fare (chiedere la shell **su richiesta** invece che a
ogni tasto destro). Va decisa prima di M8, non dopo.

**Difetto trovato contando le frecce.** Con otto pressioni la selezione si
fermava su "Incolla qui" invece che su "Proprietà". `ContextMenuMove` partiva da
0 quando niente era selezionato e poi incrementava dentro il ciclo, quindi **la
prima freccia sceglieva la seconda voce**. Guardando il menu il difetto non si
vede: si è visto solo contando quante pressioni servivano, perché il numero non
tornava di uno.

### Perché il ripristino dal cestino non è una casella da spuntare

Non è pigrizia: è che **il cestino non ha un percorso**. La nostra navigazione è
basata sui percorsi — `ReadDirectory` + `FindFirstFile` — e il cestino è una
cartella dello *spazio dei nomi della shell*, fatta di PIDL, non di file.

`C:\$Recycle.Bin` esiste come percorso, ma contiene le coppie `$I…`/`$R…`: i nomi
originali non ci sono, e mostrarli così sarebbe peggio che non mostrarli. Per
ripristinare bisogna (a) saper navigare lo spazio dei nomi, che è una **seconda
fonte di elenco**, e (b) invocare il verbo "Ripristina" del menu contestuale, il
cui nome è **localizzato** — su Windows italiano è "Ripristina", e sbagliare
verbo sul cestino significa cancellare davvero.

Quindi: la voce resta aperta, e la sua dipendenza vera è **il browsing dello
spazio dei nomi**, cioè un pezzo di M7 che oggi non c'è. Farlo prima di quello
sarebbe un imballaggio sopra una funzione che manca.

### Difetti trovati provando — *Ricostruito*

**I conflitti di nome bloccavano l'applicazione in silenzio.** Con
`FOF_NOCONFIRMATION` e senza `FOF_RENAMEONCOLLISION`, incollare un file dove ne
esisteva gia' uno non produceva ne' una domanda ne' un rinomina: l'operazione
veniva saltata, e la shell lasciava una finestra di stato modale
(`OperationStatusWindow`) che non si vedeva, perche' stava dietro la finestra
principale, ma che teneva il programma dentro il proprio ciclo di messaggi.
L'applicazione **sembrava congelata, e il difetto sembrava di rendering**: due
catture a due secondi di distanza erano identiche al pixel.

La diagnosi e' arrivata enumerando le finestre del processo, non guardando lo
schermo: la finestra che bloccava non era visibile. E' lo stesso errore di metodo
del §5 di `docs/05` — guardare il sintomo invece della causa.

**La selezione cadeva sul file sbagliato dopo un incolla con conflitto.**
Risolto con un `IFileOperationProgressSink`: i metodi `Post*` ricevono l'elemento
appena creato, ed è l'unico punto in cui il nome scelto dalla shell è
osservabile. Verificato: dopo l'incolla il cursore sta su `alfa - Copia.txt` e
non su `alfa.txt`. Prima, un `Canc` subito dopo avrebbe eliminato l'originale.

Lo stesso sink ha permesso di togliere un'euristica: `CreateFolder` indovinava il
nome finale leggendo il disco e provando i suffissi `(2)`, `(3)`, `(4)` in un
ciclo. Adesso il nome lo riporta la shell. Un valore derivato al posto di uno
indovinato, di nuovo.

**Le statistiche delle sottocartelle restavano vecchie.** Le colonne
`Folders`/`Files`/`Size` di una cartella si vedono dal suo genitore, quindi è lì
che si nota il numero sbagliato. Dopo ogni operazione ora si invalida la cartella
in cui si è lavorato. Verificato: `una-cartella` mostrava `0 | 0 | 0 B`, dopo aver
incollato un file al suo interno e essere risaliti mostra `0 | 1 | 5 B`.

*Stima originale: 4–6 settimane.* Non usare `SHFileOperation`: è deprecata.

---

## M8 — Inspector ⬜

- [ ] metadati di base e proprietà estese (`PROPSYS`)
- [ ] anteprima immagine con zoom e adattamento
- [ ] visualizzatore di testo con minimap, numeri di riga, spazi visibili
- [ ] rilevamento della codifica
- [ ] conteggio righe

*Stima: 3 settimane.* Il rasterizzatore di font proprio dell'originale
(`WinFontRasterizer.cpp`) resta fuori perimetro: GDI è sufficiente.

---

## M9 — Configurazione, hotkey, temi ⬜

**Vincolo di progetto, deciso dall'autore: niente file di impostazioni, niente
dati personali, niente tracce su disco.** Vedi `docs/06-privacy.md`. Questo
cambia la milestone rispetto a come era stata pianificata: i tre file JSON
sotto `%APPDATA%\Voidstar\FilePilot` sono il comportamento **dell'originale**, e
copiarlo significherebbe scrivere su disco i percorsi che l'utente visita. Qui
non si fa.

Quindi le voci restano, ma la persistenza **non è il default e non è implicita**:

- [ ] mappa tasti rimappabile sui 319 comandi — **in memoria**, per la sessione
- [ ] temi a 35 token, con variante chiara e scura — **in memoria**
- [ ] viste per cartella con ereditarietà e applicazione ricorsiva — **in memoria**
- [ ] layout salvabili — **in memoria**
- [ ] se un giorno la persistenza serve, dev'essere **esplicita, opt-in e locale**,
      con l'utente che sceglie il file, e con scritto nell'interfaccia cosa viene
      scritto e dove. Non una cartella creata di nascosto al primo avvio.
- [ ] nessun elenco di recenti, nessuna cronologia e nessuna anteprima lasciano la
      memoria del processo

*Stima: 3–4 settimane.*

### Cosa si perde, e perché va bene

Rinunciare alla persistenza significa che a ogni avvio l'applicazione riparte
dalle impostazioni predefinite. È un costo reale, e va detto invece di essere
scoperto. In cambio: un file manager che **non lascia traccia di cosa hai aperto**
è la scelta giusta per uno strumento che vede l'intero disco, e il progetto è
personale, quindi la configurazione che si perde è quella di una persona sola.

Il precedente nella tabella dello spec resta `StoreRecents: Session`: i recenti di
sola sessione non sono una limitazione da colmare, sono **il comportamento
voluto**.

---

## M10 — Backend Direct3D 11 ⬜

L'originale espone `GraphicsBackend` con `Direct3D` e `OpenGL` e importa
`D3DCompiler_47`. Un clone fedele deve poter scegliere.

- [ ] astrazione del renderer dietro un'interfaccia
- [ ] backend D3D11 con la stessa semantica di quad
- [ ] compilazione HLSL degli stessi shader, con cache
- [ ] selezione a runtime e fallback

*Stima: 2–3 settimane* se M1 ha definito bene l'interfaccia; molto di più se no.

---

## Ordine consigliato per un progetto con scadenza

Se il tempo è limitato, la sequenza che massimizza il risultato dimostrabile è
**M1 → M2 → M5 → M6**, con **M3** come approfondimento se resta tempo.

Il motivo è che M1 più M2 più M5 producono un file manager realmente usabile, con
un'architettura di rendering non banale da mostrare. M3 è la parte tecnicamente
più caratteristica del prodotto originale, ma resta un modulo isolato: va
affrontata con un percorso di fallback a enumerazione semplice, in modo che un
risultato parziale non comprometta il resto del progetto.

---

## Registro del reverse

Man mano che il reverse mirato procede, ogni domanda risolta va annotata qui con
la funzione di origine e il risultato.

| # | Domanda | Funzione | Stato | Risultato |
|---|---|---|---|---|
| 1 | ~~aggregazione MFT~~ | — | **falsificata** | Non c'è parsing MFT: manca la firma `FILE` in `.text` e le stringhe `NTFS`/`$MFT`/`USN`. Vedi `docs/01`. |
| 2 | dove viene controllato il budget di tempo di `SumDirStats` | `FUN_140070660` / `FUN_140070ae0` | **parziale** | Ricostruita la macchina: job creato da `FUN_140070660` con buffer ≥ 1 MB e callback `FUN_140070ae0`; l'invalidazione è per generazione (`DAT_140249268`). Il budget di tempo non è ancora localizzato. |
| 3 | parametro differenziante SSD/HDD/rete | da individuare via `Setting_CalcFolderSizesSSD` | aperta | — |
| 4 | formula del punteggio recenti | da individuare via `"Score": %d,` | aperta | — |
| 5 | semantica di `Ordinal` | idem | aperta | — |
| 6 | gestione reparse point e hard link | da individuare | aperta | — |
| 7 | uso preciso di `DeviceIoControl` | unico chiamante | aperta | Ipotesi: `FSCTL_GET_NTFS_FILE_RECORD` per la tabella `AT_*`. |
| 8 | quale funzione è `SumDirStats` e quale `LoadDirectory` | `FUN_1401e81c0` vs `FUN_1401fdfe0` | aperta | Entrambe chiamano `FindFirstFileExW`+`FindNextFileW` e sono ricorsive. Discriminare confrontando i callee (5 contro 18). |
| 9 | il worker di enumerazione | — | **identificato il set** | Solo due funzioni in tutto il binario chiamano `FindFirstFileExW`. Vedi `analysis/out/reverse/FINDINGS.md`. |

### Verifiche gia' eseguite su `AT_IndexRoot`

Tracciate per non ripeterle:

| Passo | Comando | Esito |
|---|---|---|
| import + overview | `rea inspect --provider ghidra --snapshot` | Ghidra 12.1.4, PE x86_64, 5 sezioni, import in 5 min 20 s |
| ricerca stringa | `rea search "AT_IndexRoot" --kind strings` | `0x14021a578` |
| riferimenti | `rea xrefs 0x14021a578` | solo `0x140247e90`, che sta in `.data` |
| tracciamento | `rea trace "AT_IndexRoot"` | nessun riferimento da codice, `not_in_procedure` |
| riferimenti al puntatore | `rea xrefs 0x140247e90` | vuoto |
| struttura tabella | scan `.data` | 16 puntatori contigui da `0x140247e50`, indicizzati per tipo di attributo |
| scan codice | displacement RIP-relative su tutto `.text` | 3 candidati, tutti **falsi positivi** (verificato decompilando) |
| nome del job | `rea search "SumDirStatsWorkUpdate"` | `0x14022d8e8` (messaggio) e `0x14022d918` (nome) |
| riferimenti al nome | `rea xrefs 0x14022d918` | `0x140070be9` -> completion handler |
| macro di enumerazione | `analysis/tools/callers.mjs FindFirstFileExW` | slot IAT `0x140219430`; solo **2** siti di chiamata |
| macro di enumerazione | `analysis/tools/callers.mjs FindFirstFileW` | 4 siti di chiamata |
| reperto | `analysis/out/reverse/FINDINGS.md` | ricostruzione completa di `SumDirStats` |

**Regola ricavata:** in questo binario non si entra dai dati. Le tabelle di
`.data` non hanno riferimenti statici; si entra dalle stringhe referenziate
dal codice (nomi di job, messaggi diagnostici) e dagli import.

Strumenti nuovi e riutilizzabili: `analysis/tools/callers.mjs` (trovai siti di
chiamata di una funzione importata, dallo slot IAT) e
`analysis/tools/refs-to.mjs` (riferimenti a un indirizzo interno).
