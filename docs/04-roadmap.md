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
- [x] **raggruppamento in griglia**: le bande di gruppo stanno anche fra le celle,
      a piena larghezza, e la riga di celle sotto di loro ricomincia da sinistra
- [x] **ridimensionamento continuo della cella**: `Ctrl`+rotella sopra la griglia, da
      32 a 256 px, e la griglia si reimpagina mentre si scorre

### Perché le celle sono alte quanto dicono di essere — *Ricostruito*

L'altezza della cella era un numero fisso scelto a occhio, e l'etichetta a due
righe finiva **sopra** la miniatura: si vedeva il nome stampato sulla foto. Ora
l'altezza si **deriva** da icona più etichetta, e nel disegno la dimensione
dell'icona viene comunque ridotta se lo spazio sopra l'etichetta non basta. Un
numero derivato non può entrare in disaccordo con le sue parti.

### Perché la geometria si calcola una volta — *Ricostruito*

La griglia impaginava le celle **per indice**: `indice % colonne` e `indice /
colonne`. È una formula che vale finché ogni riga ha la stessa altezza, e una banda
di gruppo a piena larghezza la rompe: la banda prende una riga intera, e sotto di
lei il conto delle colonne riparte da zero.

La formula è stata sostituita da una **geometria calcolata una volta**
(`GridRects`): un rettangolo per ogni riga della vista, senza lo scorrimento
dentro, rifatto solo quando cambia qualcosa da cui dipende (le righe, la larghezza
dell'elenco, la vista, il raggruppamento, l'ingrandimento). La usano tutti:
chi disegna, chi colpisce col mouse — che riceve il rettangolo *disegnato*, quindi
segue per costruzione — chi scorre per sapere quanto è alto il contenuto, e chi
tiene il cursore in vista con le frecce.

Tre conti separati che devono coincidere prima o poi non coincidono; uno solo non
può sbagliare. Ed è anche il motivo per cui il lavoro è stato fatto in un giro
suo: sotto le dita c'erano il disegno, il clic, la rotella e la tastiera.

### `Icons` e `Tiles` erano due punti, non due viste — *Ricostruito*

`Icons` e `Tiles` differivano **solo** per la misura: icona 64 px contro 128, cella
104 contro 176. Ed erano due numeri fissi in una `switch`, con due rapporti diversi
fra cella e icona (104/64 di qua, 176/128 di là) per la stessa cosa.

Ora c'è **una misura sola** (`gridIconSize`) e tutto il resto si deriva: la cella è
l'icona più lo spazio per l'etichetta, l'altezza è l'icona più due righe di testo. I
due comandi restano, come **scorciatoie** ai due punti noti (64 e 128); `Ctrl`+rotella
porta la misura dove si vuole, fra 32 e 256, e la griglia si reimpagina mentre si
scorre.

**Il nome della vista segue la misura**: sotto i 96 px si chiama `Icons`, sopra
`Tiles`. È una conseguenza del principio di `AGENTS.md` §6 — un controllo che dichiara
qualcosa che la vista non sta facendo è peggio di un controllo assente — perché
altrimenti il piè di pagina avrebbe continuato a dire `Tiles` mentre si guardavano
icone da 40 px. L'ingrandimento dell'interfaccia è un'altra cosa e resta il suo:
`Ctrl`+rotella non lo tocca.

**Verificato**: quattro celle per riga a 128 px, tre a 176, cinque a 80 — e a 80 il
piè di pagina dice `Icons` mentre l'ingrandimento resta `100%`.

Il secondo corpo di glifi è fatto.

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
- [x] `HoverToSelect` — il passaggio **seleziona**, non solo evidenzia: dopo una
      pausa di 0,35 s, e non mentre si trascina o con un menu aperto
- [x] `UnderlineOnHover` — sottolineatura al passaggio, sotto il nome **visibile**
      (sotto quello intero, con un nome troncato, sarebbe una riga lunga quanto una
      parola che non c'è)
- [x] `ShowTooltips` — nome per esteso, tipo, data e dimensione; per una cartella
      anche i conteggi ricorsivi quando ci sono. Compare dopo 0,55 s **di puntatore
      fermo**: il conto riparte a ogni movimento, quindi scorrendo l'elenco non
      passa niente davanti
- [x] `SingleClick` e `SingleClickFoldersOnly` — un clic apre, e con la seconda
      chiave solo le cartelle: sui file serve ancora il doppio, che è quello che
      vuole chi accende solo la prima
- [x] `ActOnPress` — l'attivazione avviene **premendo** invece che mollando.
      Verificato con una cattura scattata a tasto ancora giù: la cartella era già
      aperta. **L'interpretazione è nostra**: il significato esatto della chiave
      nello spec non è ricostruito, e questa è la lettura che le dà un senso — senza
      di essa sarebbe un doppione di `SingleClick`
- [x] `DoubleClickToParent` — doppio clic sul **fondo** della lista: su di un
      livello. Su una riga il doppio clic apre, che è un'altra cosa
- [x] `ShowCheckboxes` — una casella che **riflette la selezione** (una casella con
      una verità propria sarebbe una seconda verità da tenere allineata, e prima o
      poi le due divergono). Si preme per scegliere: il clic non apre, non arma un
      trascinamento e non sposta il cursore. Nella griglia sta nell'angolo della
      cella, che è centrata; nell'elenco sposta icona e nome di 22 px invece di
      coprirli. Il segno di spunta è un quadrato pieno perché nell'atlante dei glifi
      non c'è un carattere di spunta, e scriverne uno fuori dall'insieme rasterizzato
      darebbe un buco (§7.32)

**Tutte e cinque spente di partenza.** Sono *impostazioni* nell'originale, e il
doppio clic è il gesto che si aspetta chiunque: cambiarlo senza che nessuno l'abbia
chiesto vorrebbe dire cambiare il programma sotto le mani di chi lo usa. Si accendono
dalla palette, gruppo `Interaction`, che è il nome della riga nella tabella delle
chiavi.

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

**I valori di partenza dell'originale non sono ricostruiti**, e questi si
dichiarano: `ShowTooltips` acceso, `HoverToSelect` e `UnderlineOnHover` spenti. Si
accendono e si spengono dalla palette, che è il posto dove stanno le altre chiavi.

**Un difetto trovato guardando la griglia.** Cambiando vista le celle si spostano e
l'indice della riga sotto il puntatore non vuol più dire niente: il suggerimento
restava appeso a una voce che non era più quella sotto il mouse — cioè un nome
sbagliato accanto al puntatore, che è la forma peggiore di errore in una lista di
file. Il sovrappasso adesso si azzera cambiando vista e lo ricalcola il prossimo
movimento.

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
- [x] **barra laterale a sezioni** — QUICK ACCESS (i fissaggi della shell),
      BOOKMARKS, STORAGE (i volumi con occupazione e spazio libero), PLACES — tutte
      richiudibili, e la stella nella barra strumenti che aggiunge un segnalibro
- [x] **pulsanti di navigazione** nella barra strumenti: indietro, avanti, su,
      aggiorna, segnalibro — gli stessi comandi della tastiera e della palette
- [x] il terzo contesto del componente a gruppi: il menu contestuale (fatto in M7)
- [ ] drag&drop dei tab (`TabDropMarker_*`)

### La barra laterale a sezioni, e il confronto col riferimento — *Ricostruito*

Quattro sezioni, perché le voci sono di natura diversa e chi non usa una non deve
guardarla: **QUICK ACCESS** (i fissaggi della shell), **BOOKMARKS** (nostri, in
memoria sola: `docs/06`), **STORAGE** (i volumi con la barra di occupazione e lo
spazio libero), **PLACES** (le cartelle standard, e il cestino in fondo). Le
intestazioni sono richiudibili e lo stato è ricordato **per nome**: le righe si
spostano quando i segnalibri cambiano, e una sezione chiusa non deve riaprirsi da
sola perché ne è arrivata un'altra sopra.

Il confronto col riferimento (`file pilot.png`, guardato su richiesta dell'autore)
ha dato il resto della lista, e vale la pena scrivere cosa si è preso e cosa no:

| cosa si vede nel riferimento | qui |
|---|---|
| sezioni nella barra laterale, richiudibili | fatte |
| STORAGE col volume e lo spazio libero | fatto |
| pulsanti indietro/avanti/su/aggiorna a sinistra del percorso | fatti, con la stella dei segnalibri in più |
| il bersaglio di un collegamento accanto al nome, in tono spento | fatto |
| piè di pagina con posizione, modalità di vista e ingrandimento | fatto; i contatori del motore sono passati dietro `BOLT_DEV_HUD=1` |
| icone di filtro nelle intestazioni delle colonne | fatte: un imbuto di tre barre, acceso quando la colonna filtra |
| colonne ridimensionabili col mouse | fatte: si trascina il bordo, il doppio clic lo riporta com'era |

Le due voci sono state chiuse insieme, e le decisioni che non si vedono sono queste:

- **il filtro per colonna è un elenco di valori *tolti*, non di valori tenuti.** Una
  colonna appena aperta non filtra niente, e questo si legge dalla sua lista vuota
  senza doverla confrontare con quella dei valori possibili.
- **i valori si contano all'apertura del pannello e non si riordinano mai.**
  Togliendone uno le caselle non devono saltare sotto il puntatore. Per la stessa
  ragione l'ordine è per frequenza decrescente, che non cambia mentre si spunta.
- **il pannello non si chiude quando si spunta.** Una lista di caselle si spunta a
  più riprese: è tutto il motivo per cui è una lista e non un interruttore.
- **la dimensione filtra per fasce** (`< 1 KB`, `< 1 MB`, …): un valore per file non
  filtra niente. È l'unica colonna che non usa il valore mostrato, ed è dichiarato.
- **il nome non ha il filtro**: ogni valore è distinto, e un elenco di cento nomi
  non è un filtro. Il testo libero resta nella casella di sotto, che è un'altra cosa.
- **larghi e filtri sono della vista, non della scheda** — come le colonne stesse. Il
  filtro di testo invece è della scheda, e resta dov'era. È una scelta, non una
  dimenticanza.
- **i valori sono al massimo 40**: oltre, l'elenco non si legge più. Dichiarato nel
  codice, dove si taglia.
- righe e valori arrivano dal componente della palette, quindi il pannello scorre e
  si filtra da tastiera senza codice nuovo: è il **secondo utilizzatore** che lo ha
  fatto estrarre, e adesso ne ha un terzo.

**La quarta voce della tabella era un difetto vero, trovato guardando.** Un
collegamento mostrava `collegamento-prova.lnk` e basta: cosa apre lo sa solo la
shell, e chiederlo costa una `IShellLink` + `IPersistFile::Load` per file — una
volta per percorso, perché una riga si disegna sessanta volte al secondo.

E il piè di pagina diceva `60 fps | 477 quads | 0 stats jobs`: numeri di chi scrive
il programma, in un posto dove chi lo usa cerca "dove sono" e "quanti ne ho
scelti". Adesso dice `5 items  1 selected` a sinistra e `1 / 5 | Details | 100%` a
destra — ed è la forma del riferimento.

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

### Salire da una radice di volume — *Visto*

`Backspace` e `Alt`+`↑` salgono di un livello; da `C:\` il genitore di un percorso
non esiste, e sopra un disco c'e' **"This PC"**, la cartella di spazio dei nomi dove
si scelgono le unita'. Ci si arriva con
`ShellLocation::ParsingNameOf(FOLDERID_ComputerFolder)`, che non ha un CLSID scritto a
mano nel nostro codice.

L'etichetta e' **"This PC"** e non il "Questo PC" che la shell risponde in italiano:
e' il nome del riferimento (`AGENTS.md` §3), ed e' anche la voce che apre la sezione
`PLACES` della barra laterale, prima di `Home`.

Le tre vie — pulsante, `Backspace`, `Alt`+`↑` — eseguono lo stesso comando. Prima
`Backspace` ne aveva una sua, ed e' la ragione per cui §7.42 di `docs/02` esiste.

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
- [x] le voci di **primo livello** della shell dentro il menu contestuale (Apri con,
      le voci delle estensioni installate), dopo i nostri comandi e dopo un
      separatore. La strada è quella del binario originale —
      `IShellFolder::GetUIObjectOf` → `IContextMenu` → `QueryContextMenu` →
      `GetCommandString(GCS_VERBW)` → `InvokeCommand`: vedi sotto
- [ ] ripristino dal cestino — **bloccato da una scelta strutturale**, vedi sotto
- [ ] drag&drop in ingresso e in uscita con descrizione
- [x] il menu contestuale sa **annidare**: pannelli a destra del pannello padre,
      che si aprono col sovrappasso o con la freccia destra, scorrono, si
      richiudono con `Esc` e con la freccia sinistra, e portano il `>` sulle voci
      che ne hanno uno. I sottomenu delle **estensioni** (`7-Zip`, `WinRAR`) si
      aprono e si eseguono: verificato vedendo comparire il dialogo di 7-Zip
- [x] `Nuovo file >`, fatto da noi: i tipi si leggono dalle chiavi **`ShellNew`**
      del registro e la creazione passa da `IFileOperation::NewItem`, che sceglie il
      nome definitivo (e il `(2)` dei conflitti). Il **contenuto** però lo
      scriviamo noi: `NewItem` senza un modello crea un file di zero byte, e uno
      zip di zero byte non è uno zip. Vedi sotto
- [x] `Invia a >`, fatto da noi: la cartella SendTo è una cartella vera, quindi si
      legge (`%APPDATA%\Microsoft\Windows\SendTo`, 11 voci su questa macchina in
      0,3 ms) e si esegue **lasciandoci cadere sopra la selezione**, che è quello che
      fa la shell. Non con `ShellExecuteEx`: vedi §7.22 di `docs/02`
- [x] **drag&drop dentro e fuori**, con OLE: file trascinati da Esplora risorse
      dentro la lista (su una riga di cartella o sul fondo), file trascinati da
      Bolt in un'altra finestra, Ctrl copia / Shift sposta / stesso volume sposta,
      destinazione tratteggiata durante il gesto, e i casi vietati che non fanno
      niente invece di fare qualcosa di sbagliato (§7.23 e §7.24 di `docs/02`)
- [x] **trascinare sul riquadro di navigazione**: le sue righe sono bersagli come
      le altre, quindi `TargetAt` chiede la geometria a `HitTest` invece di
      rifarla. E la destinazione non è sempre una cartella: sul **cestino** il verbo
      è *buttare via*, non *mettere dentro* — non si copia nel cestino, e Ctrl non
      trasforma il gesto in una copia. Vedi §7.33 di `docs/02`
- [x] **trascinare *fuori* da una cartella di spazio dei nomi**: le voci portano
      l'oggetto dati della shell invece di un nome, e l'oggetto lo chiede **la
      cartella che le possiede** — montarlo a mano lo faceva accettare e non fare
      niente (§7.34 di `docs/02`). Verificato per una cartella normale; **per una
      voce riciclata la destinazione dichiara "copia" e non copia**: limite
      dichiarato, non risolto
- [x] **navigazione nello spazio dei nomi** e **ripristino dal cestino**: il
      cestino è un collegamento nella barra laterale, si apre come una cartella,
      e "Ripristina" è il verbo della shell sulla voce — che esiste solo se alla
      shell si passa il **PIDL** e non il nome (§7.25 di `docs/02`). Le voci che
      vogliono un percorso sono spente, e si vede
- [ ] i modelli col valore **`Command`** restano fuori dal menu `Nuovo file`:
      quel valore avvia un'applicazione invece di creare un file
      (`msaccess.exe /NEWDB 1`, `Wab.exe /CreateContact "%1"`), e imitarlo
      significherebbe eseguire righe di registro arbitrarie
- [ ] la rinomina della voce nuova mette il cursore **in fondo** al nome; Esplora
      risorse seleziona la parte senza estensione, così si scrive sopra senza
      cancellare il `.txt` — sulla voce nuova la selezione è già comoda
- [x] il menu della **cartella**, quando non c'è selezione, cioè il tasto destro
      sul fondo della lista: è il menu della *vista*, chiesto con
      `IShellFolder::CreateViewObject(IID_IContextMenu)`, **non** quello della
      cartella come elemento. Il clic destro sul fondo azzera la selezione, come
      in Esplora risorse, e da lì arrivano `Proprietà`, `Apri nel Terminale`,
      `Apri con …` e le voci delle estensioni
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

**Il fondo della cartella è un altro contesto, e chiederlo male non dà errore.**
Con niente selezionato il menu della vista si chiede con
`CreateViewObject(IID_IContextMenu)`; chiedere invece la cartella *come elemento*
(`GetUIObjectOf`) restituisce un menu plausibile e pericoloso: `Taglia`, `Copia`,
`Elimina` **sulla cartella aperta**. Il registro l'ha mostrato — sei voci con
`cut`, `copy`, `delete` che nessuno si aspetta a destra di un fondo. Vedi §7.18 di
`docs/02`.

**I sottomenu hanno due proprietari, e solo uno si può servire.** Un sottomenu di
un'**estensione** è disegnato da lei e i suoi comandi si eseguono con
`InvokeCommand`, come le voci radice: verificato col dialogo di 7-Zip. Un
sottomenu che la shell **popola da sé** (`Nuovo`, `Invia a`, `Dare accesso a`) si
legge benissimo — `Nuovo` ne ha venti — ma l'esecuzione risponde `S_OK` e non fa
niente, perché la shell consegna quei comandi alla finestra del menu.

Quindi: il componente annida, i sottomenu delle estensioni si offrono, quelli
della shell si filtrano per verbo canonico. Vedi `docs/02` §7.19. Un elenco che
risponde `S_OK` e non fa niente è peggio di un elenco che manca.

### Il menu "Nuovo file", fatto da noi — *Ricostruito*

La shell il proprio "Nuovo" lo popola in un sottomenu i cui comandi non si
possono eseguire da fuori (vedi sopra), quindi il menu se lo costruisce Bolt. Le
fonti sono le stesse che usa la shell, e sono **tre**, non una:

| dove sta `ShellNew` | esempio |
|---|---|
| sotto la chiave dell'estensione | `.zip\ShellNew` (WinRAR) |
| sotto un ProgID **annidato** nella chiave | `.docx\Word.Document.12\ShellNew` |
| sotto il ProgID di default dell'estensione | `MQL5.File\ShellNew` |

La seconda è la più usata su Windows 11 e senza di essa il menu avrebbe un quinto
delle voci: sul banco di prova, 18 modelli invece di 4. Sull'elenco del registro,
la **shell non offre nessuna API**: il menu lo costruisce internamente, e le
chiavi vanno lette a mano (`src/shell_new.cpp`).

**Le etichette sono stringhe indirette** (`@C:\WINDOWS\system32\zipfldr.dll,-10194`)
e si risolvono con `SHLoadIndirectString` — non con `RegLoadMUIString`, che sulla
stessa stringa restituisce un errore e lascia il menu in **inglese** mentre tutto
il resto è in italiano. Vedi §7.20 di `docs/02`.

**Il contenuto lo scriviamo noi.** `IFileOperation::NewItem` sceglie il nome
definitivo — è l'unico modo di avere il `(2)` dei conflitti senza indovinarlo —
ma senza un modello crea un file di zero byte. I valori del registro sono tre:
`NullFile` (vuoto), `FileName` (copia di un file modello) e `Data` (i byte).
Verificati sul banco di prova: `Nuova cartella compressa.zip` esce di **22 byte** e
si apre come zip valido, `Nuovo foglio di lavoro di Microsoft Excel.xlsx` di
**6.111 byte**, cioè la copia vera del modello di Office.

Due cose che la verifica ha scoperto e che nessuna lettura avrebbe dato: i modelli
di Office e di LibreOffice sono **gusci di zero byte** su disco (il contenuto lo
produce il loro provider, che da fuori non si raggiunge), e il valore `Data` di
`.rtf` è una **stringa** `{\rtf1}` e non un blocco binario. Il primo caso adesso
si dichiara all'utente invece di consegnargli un `.docx` che Word rifiuta di
aprire (§7.21 di `docs/02`); il secondo si accetta in tutte e due le forme.

**I modelli col valore `Command` restano fuori.** Quel valore non crea un file:
avvia un programma. La lettura del registro, quando serve, costa **meno di un
millisecondo** (misurata: 0,7 ms per 20 modelli) e avviene alla prima apertura del
sottomenu, non a ogni tasto destro.

### Il menu "Invia a", fatto da noi — *Ricostruito*

Qui la fonte non è nascosta come le chiavi `ShellNew`: **"Invia a" è una cartella
vera**, `%APPDATA%\Microsoft\Windows\SendTo`, e un file lì dentro è una
 destinazione. Si legge a ogni apertura, non una volta sola come il registro,
perché chi ci mette dentro un collegamento nuovo si aspetta di vederlo; il costo è
una `FindFirstFile` su una manciata di file — misurato: **0,25–0,32 ms** per 11
voci. L'etichetta è il nome del file senza estensione, come in Esplora risorse,
quindi si possono vedere due lingue nella stessa lista: i file sono stati creati
in momenti e installazioni diverse, e la shell mostra gli stessi nomi.

**Eseguirla, invece, non è avviarla.** La voce "cartella compressa" della cartella
SendTo non è un programma: è un file `.ZFSendToTarget` che contiene un CLSID, e il
suo handler è registrato come `DropHandler` — il registro lo dice con le parole
*"Drag Files onto this icon to compress them"*. `ShellExecuteEx` su quella voce
restituisce `TRUE` e non fa niente: nessun errore, nessun file, che è la peggiore
delle risposte possibili (§7.22 di `docs/02`).

Si fa quindi quello che farebbe il mouse: si prende il **bersaglio di caduta**
della voce (`IShellFolder::GetUIObjectOf` con `IID_IDropTarget`) e gli si lascia
cadere sopra la selezione, come `IDataObject` costruito con `SHCreateDataObject`
dai PIDL dei percorsi scelti. `DROPEFFECT_COPY`, perché "invia a" manda una copia e
non porta via l'originale. Vale per tutte le voci della cartella, compresi i
collegamenti ai programmi, che si aprono con i file ricevuti. Verificato da capo a
fondo: la voce che comprime ha prodotto **`relazione.zip` dentro la sandbox** (218
byte, con `relazione.txt` dentro), e la barra di stato ha scritto *"Avviato:
…"*.

"Avviato" e non "fatto", perché la destinazione comprime, copia o apre una
finestra sua e il suo esito non è nostro da dichiarare; se invece la voce non
accetta i file, lo dice.

### Il trascinamento, che è di OLE — *Ricostruito*

Trascinare fra due programmi non è un accordo fra due programmi: è un **oggetto
dati** che il sistema consegna alla finestra sotto il puntatore. Da qui due metà,
e servono tutte e due:

| verso | interfaccia | chi la fornisce |
|---|---|---|
| in entrata | `IDropTarget` registrato con `RegisterDragDrop` | noi, sulla nostra finestra |
| in uscita | `IDropSource` + `DoDragDrop` | noi, quando il mouse si muove con il tasto giù |

L'oggetto dati è quello della shell (`SHCreateDataObject`, lo stesso che scrive
Esplora risorse), quindi una cartella o un altro file manager lo accettano senza
sapere chi gliel'ha mandato. Il modulo che se ne occupa è `src/drag_drop.cpp`; la
destinazione sotto il puntatore la decide `main.cpp`, perché solo lì si sa cosa
c'è sotto — una riga di cartella, o il fondo della lista (che vale la cartella
aperta).

**La regola copia/sposta è quella di Esplora risorse**, e sono tre casi: Ctrl
copia, Shift sposta, e senza tasti si guarda il volume — stesso volume sposta,
volume diverso copia. Un file trascinato nella cartella dove è già non si sposta:
lì non c'è niente da fare, e il puntatore lo dice.

**L'evidenziazione della destinazione si disegna da un callback di OLE.** Durante
un trascinamento che nasce da qui il ciclo principale è fermo dentro `DoDragDrop`,
quindi la riga non si accenderebbe mai: il ciclo modale di OLE consegna però i
messaggi, e un fotogramma disegnato quando la destinazione **cambia** basta. È
`RenderFrame`, l'unico posto in cui si disegna fuori dal ciclo.

**Quattro cose misurate**, tutte con `BOLT_DIAG=1` → `bolt-diag.log`:

- un trascinamento in entrata arriva con i percorsi già pronti (`1 percorsi`);
- un trascinamento **in uscita** verso Esplora risorse: `effetto restituito 0,
  esito dichiarato 2` — la destinazione sposta e lo dichiara in un formato
  dell'oggetto dati, non nel valore di ritorno (§7.23 di `docs/02`);
- un trascinamento rifiutato: `esito dichiarato 0`, e la cartella resta identica
  (§7.24);
- la vista si rifà da sola dopo uno spostamento, senza premere F5.

Prove fatte, con `tools/drag-mouse.ps1` (preme, muove a passi, **fotografa a metà
gesto**, rilascia) e `tools/explorer-side.ps1`:

| prova | esito |
|---|---|
| `nota.txt` da Bolt sulla riga della cartella `documenti` | spostato, riga tratteggiata a metà gesto |
| `relazione.txt` da Esplora risorse al fondo della lista di Bolt | spostato, tratteggio sul bordo della lista |
| `contratto.txt` da Bolt a Esplora risorse | spostato dalla shell, vista rifatta da sola |
| un file sul fondo della **propria** cartella | rifiutato: nessun `(2)`, registro `esito dichiarato 0` |

### Le cartelle che non sono percorsi — *Ricostruito*

Il cestino e "Questo PC" non hanno un percorso: si raggiungono con un nome che
comincia per `::`, e si aprono con `SHParseDisplayName`. Il CLSID non è scritto a
mano nel codice — si chiede con `SHGetKnownFolderIDList` e
`SHGetNameFromIDList` a chi lo sa.

```cpp
SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, ...)  → PIDL
SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING) → "::{645FF040-…}"
```

Le voci si enumerano con `IShellFolder::EnumObjects` e diventano `Entry` normali,
con le stesse funzioni di formattazione delle altre: **così lista, ordinamento,
filtro e gruppi non hanno dovuto sapere niente** di nuovo. Da qui in poi una voce
del cestino è una voce come le altre.

Due cose non si potevano indovinare:

- **il nome** di queste cartelle non lo sa l'oggetto. Per "Questo PC" risponde col
  nome del computer; il nome vero sta in `HKCR\CLSID\{…}\LocalizedString`, la
  stessa stringa indiretta delle etichette del menu "Nuovo" (§7.26 di `docs/02`);
- **l'identità** di una voce è il suo PIDL, non il suo nome. Il nome di un file nel
  cestino è il percorso del file *dentro* `$Recycle.Bin`: chiedendo il menu di quel
  nome la shell risponde col menu di quel file, senza alcun errore (§7.25).

**Cosa non si fa, e si vede.** Dentro una cartella di spazio dei nomi le voci che
vogliono un percorso — Taglia, Copia, Rinomina, Elimina, Incolla, Nuova cartella,
Nuovo file, Invia a, terminale, Proprietà — sono **spente** nel menu contestuale:
li' le voci si gestiscono coi verbi della shell, che quegli oggetti li conoscono.
Non si trascina fuori da li' (l'oggetto dati si costruisce dai nomi, e sarebbero i
nomi sbagliati), e non si entra in una cartella riciclata: si ripristina.

**Verificato** (con un file della sandbox messo nel cestino e poi filtrato):

| prova | esito |
|---|---|
| "Questo PC" aperto dalla riga di comando | due unità, "Questo PC" in briciola e nel titolo della scheda |
| entrare in un'unità da "Questo PC" | briciola `C: >` |
| cestino dalla barra laterale | si apre, si chiama "Cestino", colonne Tipo e Data compilate |
| menu contestuale su un file riciclato | **`Ripristina` verbo `undelete`**, e le nostre voci spente |
| "Ripristina" eseguito | il file torna nella sandbox col contenuto intatto |
| filtro attivo, poi F5 | il filtro resta e la lista resta filtrata (§7.28) |

**Quattro costi misurati** (registro: `BOLT_DIAG=1` → `bolt-diag.log`):

Due note sull'elenco, perché sono differenze misurate rispetto al menu della
shell e non difetti noti:

- *"Documento di testo" la shell lo conosce da sé.* Nessuna delle 37 chiavi
  `ShellNew` di questa macchina nomina `.txt`, eppure Esplora risorse lo offre. La
  voce si aggiunge a mano, con l'etichetta chiesta a `SHGetFileInfo` (la stessa
  fonte della colonna "Tipo", quindi tradotta). Un `.txt` vuoto è esattamente
  quello che si vuole.
- *"Immagine bitmap" invece non si aggiunge.* Quella la shell la crea con un
  contenuto suo che il registro non dichiara: la nostra sarebbe un file di zero
  byte con l'estensione `.bmp`, cioè un'immagine che nessun programma apre.
  Meglio nessuna voce di una voce che non funziona.

**Quattro costi misurati** (registro: `BOLT_DIAG=1` → `bolt-diag.log`):

| misura | valore | lettura |
|---|---|---|
| prima apertura dopo l'avvio | ~350 ms | le DLL delle estensioni si caricano in-process |
| regime | ~65 ms | il costo di interrogare i gestori registrati, a ogni tasto destro |
| fondo della cartella | ~220 ms la prima volta | `CreateViewObject` carica un altro giro di gestori |
| handle di kernel | +22 per apertura — **una perdita, non un costo** | non tornavano mai giu': chiusa in §7.38 di `docs/02` con una memoria per selezione |
| estensioni moderne Win11 | dalla **seconda** apertura | le voci dei gestori moderni (`Apri nel Terminale`, `Apri con Code`) si registrano solo al secondo giro |

L'ultimo è un limite dichiarato, non risolto: non dipende da noi.

**La scelta è stata fatta** (§7.38 di `docs/02`): la shell si chiede **una volta per
selezione** e il risultato si tiene in memoria per 30 secondi. Il tempo di una
selezione nuova resta (~65 ms), ma aprirlo due volte non lo paga due volte — e
soprattutto la perdita di handle è sparita: dieci menu di fila adesso lasciano il
contatore fermo, sia sulla stessa selezione sia su dieci righe diverse. La scadenza
di 30 secondi non è un numero a caso: la prima interrogazione della sessione
risponde un elenco **incompleto** (21 voci contro 24) perché in Windows 11 i gestori
moderni si registrano al secondo giro.

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

## M8 — Inspector 🟡

- [x] metadati di base: tipo, dimensione, **creazione, modifica, accesso**, percorso
- [x] proprietà che il file system non dice, lette da `IPropertyStore`:
      **dimensioni dell'immagine** (`64 x 64 px`), e per i **volumi** il file system
      e lo spazio libero
- [x] selezione multipla: conteggio per tipo ("3 Documento di testo, 2 Cartella di
      file, 1 Excel.CSV"), somma delle dimensioni, cartelle e file
- [x] anteprima del file selezionato nella sua cella grande
- [x] **visualizzatore di testo**: righe con numeri, **minimap** con la parte
      visibile, **spazi visibili** (un punto per uno spazio, una freccia per una
      tabulazione), andare a capo, ingrandimento, e la rotella che scorre il testo
      (con Ctrl lo ingrandisce). `InspectorShowWhitespace`,
      `InspectorHideLineNumbers`, `InspectorHideMinimap`, `InspectorWordWrap`,
      `InspectorZoomLevel`
- [x] rilevamento della codifica (UTF-8, UTF-8 con BOM, UTF-16 LE e BE, ANSI) e
      conteggio delle righe, con i terminatori (`CRLF`, `LF`, `CR`, `misto`)
- [x] **anteprima immagine con zoom e adattamento**: il file si decodifica davvero
      (WIC, quindi JPEG, PNG, BMP, GIF, TIFF e i codec installati) e si carica in un
      **sesto strato** dell'array, da 1024 px. Niente secondo campionatore e niente
      ramo nello shader: ingrandire è geometria di un quad
- [x] le 20 etichette `PanelInspector_Label*`: percorso originale e data di
      eliminazione arrivano dalle **colonne della shell**, con i nomi che usa lei —
      per una voce di spazio dei nomi l'inspector mostra *le sue* colonne, che sono
      la sua vista di quell'oggetto, invece di ripetere metà delle nostre
- [ ] `InspectorHideGrid`: la chiave esiste, e cosa sia la "griglia" dell'inspector
      non è ricostruito. Non si inventa

*Stima: 3 settimane.* Il rasterizzatore di font proprio dell'originale
(`WinFontRasterizer.cpp`) resta fuori perimetro: GDI è sufficiente.

### Il visualizzatore di testo — *Ricostruito*

Un file di testo non è una cosa sola: può essere UTF-8 con o senza marcatore, UTF-16
nei due versi, o "ANSI" — la codifica locale, che qui è latin-1. E può **non essere
testo affatto**, che è il caso più importante da riconoscere: aprire un eseguibile
come se fosse prosa riempirebbe il pannello di rumore, e sembrerebbe un difetto
nostro.

Quindi non si guarda l'estensione, si guarda il **contenuto**: si legge l'inizio del
file, si decide la codifica (marcatore d'ordine, alternanza dei byte zero per
l'UTF-16 senza marcatore, validità dell'UTF-8 e ripiego sulla codifica locale), e si
rifiuta quello che ha più del 2% di caratteri di controllo. Verificato: un `.bin`
non mostra nessun visualizzatore, e un `.txt` con dentro byte binari non lo
mostrerebbe nemmeno.

La riga di stato dice le tre cose che si vogliono sapere prima di leggere:
`UTF-8   10 lines   CRLF`. I terminatori si **contano** e si dicono, perché un file
con due convenzioni dentro è una cosa che si scopre dopo aprendolo con un editor
vero — e la prima versione di prova l'ha dimostrato suo malgrado: dichiarava `misto`
e aveva ragione, perché il file di prova era stato scritto con Python che traduce i
terminatori da solo.

**Un testo si legge in un carattere a spaziatura fissa**, e i quattro strati
dell'atlante erano tutti occupati: il quinto è Consolas. Senza, le colonne non si
allineano e un visualizzatore di testo serve a poco; se Consolas non c'è si ripiega
sul carattere secondario, e le colonne non si allineano — cosa che vale la pena
sapere, ed è scritta dove sta la decisione.

Due limiti dichiarati: si legge **un megabyte** (una lettura sincrona più lunga
bloccherebbe la finestra, e questo non è un editor), e oltre quello la testata lo
dice. E le righe si impaginano **una volta per misura** — con l'andare a capo acceso
una riga logica può essere più righe di schermo, e rifarlo a ogni fotogramma su un
file lungo sarebbe lavoro sprecato.

### Le proprietà che il file system non dice — *Ricostruito*

Un file system sa dire nome, dimensione e tre date. Non sa dire che un'immagine è
larga 64 pixel: quello sta nell'intestazione del file, e chi lo sa è il gestore di
quel tipo. La strada per chiederglielo è `IPropertyStore`
(`SHGetPropertyStoreFromParsingName` + `PKEY_*`), la stessa da cui Windows riempie
le colonne della sua lista, e le chiavi sono documentate in `propkey.h`.

Il **volume** invece è un'altra domanda con un'altra risposta: capacità, spazio
libero e file system si chiedono al sistema (`GetDiskFreeSpaceExW`,
`GetVolumeInformationW`) — lo dice anche `docs/03`, che ricostruisce il
raggruppamento per occupazione dei volumi proprio da quell'import.

Tutte e due le letture si fanno **una volta per percorso** e si tengono in una
mappa: l'inspector si ridisegna a ogni fotogramma.

**E una cosa che non si fa**: per un volume non si chiedono le statistiche
ricorsive. Non è una dimenticanza, è che la domanda non ha senso — e la prima
versione la faceva lo stesso, riceveva zeri e li scriveva (§7.31 di `docs/02`).

---

## M9 — Configurazione, hotkey, temi 🟡

**Vincolo di progetto, deciso dall'autore: niente dati personali, niente tracce di
cosa si è guardato.** Vedi `docs/06-privacy.md`. I tre file JSON sotto
`%APPDATA%\Voidstar\FilePilot` sono il comportamento **dell'originale**, e
copiare quelli significherebbe scrivere su disco i percorsi visitati. Qui non si fa.

La persistenza, quando c'è, è **esplicita e visibile**: `%APPDATA%\Bolt\settings.ini`,
testo leggibile con una riga per preferenza, che non contiene nomi di file né
cartelle — solo come si vuole che il programma si comporti. L'applicazione dice dove
l'ha scritto, e cancellare il file riporta tutto ai valori di partenza. La scelta
della lingua l'ha resa necessaria, ed è la stessa cosa che `docs/06` §4-bis prevede.

- [x] il **pannello delle opzioni** (`Ctrl+,` e la voce «Open options» nella
      palette): una **lista cercabile** a righe alte due linee, ognuna con nome,
      descrizione e un **controllo tipizzato** — campo di testo, stepper, slider,
      interruttore, scelta con sottoelenco. I tipi sono `OptionControl`, e la
      verità sta in `AppState`: `RefreshOptionItem` la porta nella riga,
      `ApplyOptionFromItem` la riporta indietro
- [x] preferenze ricordate in `%APPDATA%\Bolt\settings.ini`
- [ ] mappa tasti rimappabile sui 319 comandi — **in memoria**, per la sessione
- [ ] temi a 35 token, con variante chiara e scura — **in memoria**
- [ ] viste per cartella con ereditarietà e applicazione ricorsiva — **in memoria**
- [ ] layout salvabili — **in memoria**
- [ ] le opzioni che l'originale ha e qui **non** sono applicate (animazioni,
      angoli arrotondati, schema di sistema, tavolozza, opacità dei pannelli
      inattivi, estensione nei nomi, percorsi lunghi, percorsi compatti) si
      mostrano **spente**, con la ragione accanto: un'opzione che non si applica
      sta in vista e dice che non si tocca
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

### La lingua e' finita — *Visto*

Il meccanismo c'era (`Tr`/`TrW`, coppie di stringhe dove si usano); mancavano le
superfici. Adesso la lingua copre:

- il **menu contestuale** (le nostre voci: `Apri`/`Open`, `Taglia`/`Cut`, ...); le
  voci della shell restano nella lingua del sistema, che le sa lei;
- l'**inspector**, per le parti in prosa (`Niente selezionato`, `N elementi
  selezionati`, `N file, M cartelle`, `conteggio...`, `%s liberi`);
- i **messaggi di stato** (operazioni su file, shell, proprieta', cascate,
  trascinamento) e i **conteggi** con la grammatica giusta (`Copiato 1 elemento` /
  `Copiati 3 elementi`);
- la **barra di stato** (`N voci`, `N selezionati`, i quattro nomi di vista) e il
  **pie' di pagina** (`Filtra N file...`, `Raggruppa: ...`);
- il **visualizzatore di testo** (`N righe`) e il pannello dei valori di una colonna
  (`Tutti`/`Nessuno`, `Scrivi per filtrare i valori...`);
- il **nome di partenza di una cartella nuova** (`Nuova cartella` / `New folder`).

Il modulo sta in `src/language.h`/`.cpp` perche' anche `file_ops.cpp` ha i suoi due o
tre messaggi: il secondo utilizzatore e' la ragione per cui e' un file a se'.

**Cosa resta in inglese, e perche'.** I **nomi del riferimento** non si traducono
(`AGENTS.md` §3): le sei intestazioni delle colonne, le quattro sezioni della barra
laterale, le righe dell'inspector (`Type`, `Size`, `Created`, ...), i gruppi della
palette, i nomi delle bande di raggruppamento (`GroupByLabel_*`). Sono *i suoi nomi*,
e cambiarli renderebbe il clone meno fedele, non piu' tradotto.

**Verificato a schermo in tutte e due le lingue**: pie' di pagina, barra di stato,
menu contestuale, inspector a selezione multipla, messaggio di copia, e la testata
del visualizzatore (`2 righe` / `2 lines`, `UTF-8`). Vedi anche §7.41 di `docs/02`,
per il nome della vista a elenco che l'inglese ha fatto vedere sbagliato in tutte e
due.

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

### Il riferimento guardato dal vivo (v0.8.5), su richiesta dell'autore

L'autore ha chiesto di aprire il prodotto originale e navigarlo, per migliorare il
piano. L'ho aperto **sulla sandbox del progetto**, così che quello che si vedeva
fosse roba di progetto, e le catture sono state **guardate e cancellate**: due
mostravano i file recenti e le cartelle di chi lavora, e `AGENTS.md` §0 dice che una
cattura è una copia.

**Quello che conferma la ricostruzione.** La finestra principale ha le stesse
sezioni nella barra laterale (`QUICK ACCESS`, `BOOKMARKS`, `STORAGE` con la barra di
occupazione, `PLACES` col cestino in fondo), le stesse colonne
(`Name/Type/Modified/Folders/Files/Size`), il piè di pagina col filtro e
`Group: None  n / n`, l'inspector a destra e i pulsanti di navigazione a sinistra
del percorso. La nostra ricostruzione era già quella lì.

**Quello che non abbiamo, e che vale la pena mettere in fila.**

1. **Una barra inferiore** con, da sinistra: il campo di filtro, i selettori di
   vista (`2`, `3`, elenco, dettagli) e una **percentuale di ingrandimento** che
   riguarda le *anteprime*, non l'interfaccia (`0%` = nessuna anteprima). Da noi il
   filtro sta sotto la lista e i selettori di vista stanno solo nella palette.
2. **Le colonne hanno il filtro su tutte le intestazioni**, anche su `Name`: da noi
   il nome non ce l'ha, perché un valore per nome non filtra niente. Vale la pena
   riguardarlo: forse là il filtro del nome non è per valori ma per testo.
3. **Una modalità con la barra laterale di ricerca**: aprendo il pannello laterale
   compaiono `Recents` (file e cartelle recenti), una casella «Find Quick Access…»,
   `Bookmarks`, `Storage`, `Places` e le **schede**. Cioè: i recenti sono una
   sezione vera, non un elenco che si apre quando serve — ed è la cosa che M4
   chiama «ranking dei recenti».
4. Le schede ci sono anche in questa versione, e stanno sopra il percorso.

**Quello che non ho trovato.** La superficie delle **impostazioni**: il pulsante in
alto a sinistra apre e chiude la barra laterale — come da noi — e non ho trovato
dove File Pilot metta le preferenze. Quindi per M9 il piano resta quello che
abbiamo: la palette è la superficie delle preferenze, perché è lì che si accendono
le chiavi; e le preferenze **si ricordano** in `%APPDATA%\Bolt\settings.ini`,
testo leggibile con una riga per preferenza, che si cancella a mano.

### Le tre cose dell'originale che mancavano — *Viste*

**1. I selettori di vista e la percentuale delle anteprime, nella barra in fondo.**
Da noi la riga in fondo è la barra di stato, che è larga quanto la finestra, ed è
lì che stanno adesso: quattro pastiglie `Dettagli`/`Elenco`/`Riquadri`/`Icone` (la
vista attiva è accesa) e uno stepper `Anteprime − % +`. La percentuale va da **0 a
100**: `0%` spegne le anteprime e lascia l'icona del tipo, `50%` disegna la
miniatura a metà della sua casella, `100%` è la misura piena. Oltre il 100 non si
sale **dichiaratamente**: la casella di una cella è quella, e una miniatura più
grande finirebbe sopra l'etichetta. La scelta si ricorda in `settings.ini`.

**2. Il filtro sulla colonna `Name`.** Il nome non ha un elenco di valori — ogni
nome è diverso — quindi la sua linguetta non apre il pannello delle caselle come le
altre colonne: **accende la casella di testo in fondo**, che è il filtro del nome
che serve. La linguetta si accende quando il filtro è in uso, come quella delle
altre colonne quando filtrano.

**3. La sezione `RECENTS` nella barra laterale.** Le cartelle visitate in questa
sessione, la più recente per prima, con il nome della cartella (non il percorso). È
il **ranking dei recenti di M4** nella sua forma minima: solo cartelle, e solo la
sessione (`StoreRecents: Session`), perché `docs/06` vieta di lasciare traccia di
cosa si è aperto. Un recente che è la cartella aperta non si mostra: riproporre dove
si è già non serve.

### L'originale ha un pannello di opzioni, non una lista di comandi — *Visto*

L'autore ha aperto il prodotto originale e ne ha mandato due catture, su sua
richiesta. Da quelle si ricostruisce la superficie che M9 deve avere, e si corregge
una cosa che avevamo dato per buona.

**Il menu principale** (il pulsante in alto a sinistra) contiene:

| voce | scorciatoia |
|---|---|
| `GoTo` | `Ctrl+P`, `F4` |
| `Commands` | `Ctrl+Shift+P` |
| `Load Layout` / `Save Layout` | — |
| `Options` | `Ctrl+,` |
| `File Pilot Setup` | — |

**Le nostre due scorciatoie erano scambiate rispetto all'originale**: da noi la
palette dei comandi stava su `Ctrl+P` e il GoTo su `Ctrl+G`. Allineate: `Ctrl+P` (e
`F4`) al GoTo, `Ctrl+Shift+P` ai comandi, `Ctrl+,` alle opzioni. `Ctrl+G` resta
libero; ciclare il raggruppamento sta su `Ctrl+Shift+G`.

**Il pannello delle opzioni, adesso, c'e'.** E' una lista piatta e cercabile
(`GroupMenu` con `richRows`), alta due linee per voce, aperta con `Ctrl+,`. I
controlli sono tipizzati (`OptionControl`): testo (`Font`, `Inspector Font`),
stepper (`Font Size`, `Inspector Font Size`), slider (`Spacing`, `Zoom`, misura
della cella), interruttore (tutte le chiavi di interazione e `Alternating Row
Colors`, `Show The Sidebar`), scelta con sottoelenco (`Language`). Le voci che
l'originale ha e che qui non sono applicate si mostrano **spente**, con la ragione
accanto.

**Il pannello delle opzioni dell'originale** e' una **lista cercabile**, non una
griglia di controlli: in cima un campo «Select an option...», sotto una voce per
opzione, ognuna con tre cose —

1. il **nome** (`Font`, `Font Size`, `Spacing`, `Animations`, ...),
2. una **descrizione** che dice a cosa serve (`Defines the space between items in a
   list.`): il *perche'* sta accanto alla scelta, non nella documentazione,
3. un **controllo del tipo giusto** per quella opzione:
   - campo di testo (`Font` → un nome di font installato),
   - **stepper** `− 9 +` (`Font Size`),
   - **slider** (`Spacing`, `Inactive Panel Content Opacity`),
   - **interruttore** (`Animations`, `Round Corners`, `Alternating Row Colors`,
     `Show File Extension`, `Long Paths Support`, `Display Compact Paths`),
   - **pulsante con freccia** per le scelte che aprono qualcosa (`Color Scheme` →
     `Voidstar >`).

In fondo, la **tastiera scritta nel pannello**: `Up` `Down` navigano, `Invio` usa,
`Esc` chiude. E' la stessa promessa che il nostro `GroupMenu` fa nel suo piè di
pagina, quindi il componente c'e' gia': quello che manca sono i **controlli tipizzati**
dentro le righe, che oggi sono tutte testo.

**Una lezione di interfaccia da copiare**: `System Light / Dark Scheme` si vede
**spento**, perche' li' un tema e' gia' stato scelto a mano. Un'opzione che non si
puo' applicare si mostra disabilitata invece di sparire — la stessa regola per cui da
noi le voci di menu che vanno su un percorso sono spente dentro una cartella di
spazio dei nomi.

**Opzioni viste**, nell'ordine in cui compaiono: `Font`, `Font Size`, `Inspector
Font`, `Inspector Font Size`, `Spacing`, `Animations`, `Round Corners`, `System
Light / Dark Scheme`, `Color Scheme`, `Alternating Row Colors`, `Inactive Panel
Content Opacity`, `Show File Extension`, `Long Paths Support`, `Display Compact
Paths`, e altre sotto il bordo.

**La lingua** non compare fra queste: sta piu' in basso o altrove. Da noi la scelta
della lingua c'e' gia' ed e' nella palette, che per ora e' anche la nostra superficie
delle preferenze: quando arrivera' il pannello delle opzioni, la lingua ci si sposta.
