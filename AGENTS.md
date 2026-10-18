# AGENTS.md

Istruzioni operative per chi lavora su questo repository — umano o agente.

---

## 0. Regola assoluta — leggere prima di ogni altra cosa

> **Nessun file personale va toccato.** Né sul Desktop, né in Documenti,
> Immagini, Download, Video, né altrove — **nemmeno per provare, nemmeno per
> verificare, nemmeno in sola lettura**.
>
> **Nessuna informazione personale viene salvata.** Niente screenshot che
> mostrano il contenuto delle cartelle di una persona, niente elenchi di nomi di
> file, niente percorsi personali copiati nei documenti o nei messaggi di commit.

Questa regola non ha eccezioni e non si negozia con la comodità.

### Come si fanno le prove, allora

Le prove si fanno **dentro il progetto**, in una cartella usa e getta creata allo
scopo. C'è un copione che la prepara:

```bash
powershell -ExecutionPolicy Bypass -File tools/make-test-sandbox.ps1
# -> analysis/tmp/sandbox, con contenuto finto e sempre uguale
```

Il contenuto è finto, deterministico e ripetibile: due cartelle, qualche file di
testo, un CSV e qualche PNG generata (colori distinti: sono le anteprime da
guardare). `analysis/` è esclusa dal controllo di versione,
quindi niente di tutto questo finisce nel repository.

Mai usare il Desktop, i Download o le Immagini di chi lavora, **nemmeno come
"cartella di esempio"** per una cattura.

Quando una prova **deve** mostrare il riquadro di navigazione — per esempio una
caduta su una sua voce — vale questa procedura, che non lascia nomi personali in
nessun file:

1. si chiude `QUICK ACCESS`, che è l'unica sezione coi fissaggi di chi lavora:
   **si clicca la sua intestazione**, che è la prima riga e sta a un `y` calcolabile
   dal codice. Le altre tre sezioni (`BOOKMARKS`, `STORAGE`, `PLACES`) contengono
   solo nomi standard e segnalibri del progetto.
2. si verifica che sia chiusa catturando **solo la sua riga** (`capture-rect.ps1`,
   coordinate schermo): la freccia `>` invece di `v` è la risposta.
3. si cattura **solo il riquadro** con `tools/capture-rect.ps1`, mai la finestra
   intera: quel copione salva il rettangolo chiesto e niente altro.

Un clic su una finestra **non attiva** la attiva e basta, e viene consumato: il
primo tentativo di chiusura non chiude niente, e la cattura successiva mostra i
fissaggi. Quando si fa una prova di questo tipo si guarda il punto 2 **prima** di
salvare altro.

### Perché questa regola esiste

Chi lavora qui è una **macchina**. Un agente che ispeziona un disco vede nomi di
file, percorsi, foto, documenti, e li vede molto più in fretta di quanto una
persona possa accorgersene. Che li veda mentre costruisce il programma è
inevitabile. Che ne **conservi una copia** no.

Quindi valgono tutte queste:

- Nessuno screenshot, immagine o file di prova che contenga contenuto personale.
- Nessuna telemetria, nessun invio di dati, nessun "campione" di percorsi reali.
- Nessuna informazione viene trattenuta oltre il lavoro che la richiede.
- Se una verifica richiede per forza dati reali, **si chiede**, e si spiega
  perché; non si decide da soli che quella volta è innocua.

### Il cestino, in particolare

Il cestino **è** una cartella di dati personali. Aprirlo per provarlo mostra i
nomi di quello che è stato buttato da qualcuno, e una cattura li copia. Quindi:

- per provarlo si mette nel cestino **un file della sandbox**, mai un file reale;
- si restringe la lista col **filtro** a quel file, e solo dopo si fotografa;
- la verifica di un ripristino si fa **contando i file nella sandbox**, non
  guardando il cestino;
- mai una cattura del cestino senza filtro. È già successo il 2026-10-12: due
  catture, cancellate subito, e la lezione sta in `docs/05`.

### Il riferimento di File Pilot

Sul Desktop dell'autore c'è una cattura del prodotto originale, `file pilot.png`,
usata come confronto di funzioni e grafica. **Si apre solo quando l'autore lo
chiede**, mai di iniziativa: mostra le cartelle personali di chi lavora, e una
cattura è una copia. Non si copia nel repository, non se ne citano i nomi che si
vedono dentro, e se ne prendono solo le **decisioni di interfaccia** — quali
pannelli esistono, cosa mostrano, dove stanno.

### La scrivania di chi lavora, in particolare

Chi usa questo computer ci lavora **mentre** l'agente lavora, e su Windows può avere
più **scrivanie virtuali**: una per il suo lavoro, una per le prove. Lo schermo non è
un banco di prova privato, e le finestre delle prove non devono comparire dove non
sono state chieste.

Succede esattamente questo se non ci si pensa (è successo il 2026-10-17):

- **un programma lanciato finisce sulla scrivania che è attiva in quel momento.** Se
  chi lavora sta studiando sulla scrivania 2, il `Bolt.exe` di prova nasce **lì**;
- **portare in primo piano una finestra cambia la scrivania che si vede.** Windows
  passa alla scrivania della finestra attivata: chi studiava si ritrova davanti il
  banco di prova;
- **una finestra di Esplora risorse resta** dove è stata aperta, e chi torna alla
  scrivania 1 la trova lì senza sapere perché.

Quindi, per le prove che hanno bisogno dello schermo:

1. **si fanno solo quando chi lavora dice che la macchina è libera**, e si dice cosa
   si sta per aprire;
2. **non si porta in primo piano niente** senza prima aver verificato che la finestra
   sia sulla scrivania virtuale **corrente** (`tools/focus-window.ps1` lo controlla e
   si rifiuta: `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop`);
3. **si chiude tutto quello che si è aperto** — la finestra di Esplora risorse
   compresa, che non se ne va da sola — e si tolgono i `topmost` eventualmente messi;
4. **nessuna finestra di prova si sposta sull'altra scrivania** di chi lavora: se una
   prova richiede due scrivanie, quella prova non si fa.

Il resto di `AGENTS.md` vale dentro questa regola: le prove si fanno sul banco, e il
banco non si prende il computer di chi ci lavora.

---

## 1. Cos'è questo progetto

**Bolt** è un clone didattico di **File Pilot** (file manager nativo per Windows di
Voidstar d.o.o.), ricostruito da zero a partire dall'analisi statica del binario
distribuito. Progetto personale, uso personale, non commerciale.

- Nessuna riga di codice dell'originale è copiata. Il comportamento è ricostruito
  da evidenze: stringhe, risorse, pseudocodice, prove a schermo.
- Il nome **File Pilot** designa il prodotto originale e **non va mai rinominato**:
  compare nei percorsi dati (`%APPDATA%\Voidstar\FilePilot`), nei percorsi sorgente
  recuperati dal PDB e nelle sezioni che ne descrivono il comportamento.
- Il repository è <https://github.com/gianmarcotaf02/bolt>, ramo `main`.

---

## 2. Regola zero: niente è "fatto" finché non è visto

> Ciò che non è verificato a schermo o contro disco non è *fatto*: è *scritto*.

L'autore ha chiesto fin dall'inizio di **verificare sempre a schermo**, mai a
impressione. Questo non è un consiglio, è il metodo del progetto.

Il ciclo è: **costruire → lanciare → catturare → guardare la PNG**.

Conseguenze pratiche:

- Un test che passa non basta se non hai guardato il risultato.
- Un'asserzione su un valore incerto si verifica **scrivendola in un log e
  leggendola**, non deducendola dal codice o dalla documentazione.
- Se un difetto è visibile all'utente e invisibile ai tuoi strumenti, **lo
  strumento è sbagliato**, non il difetto inesistente. È già successo: vedi §7.

---

## 2-bis. La mappa prima del territorio — interroga il grafo, non il sorgente

> **Regola, per ogni richiesta che tocca il codice: il primo passo è una query al
> grafo, e la lettura è il secondo.** Vale sempre — anche quando credi di sapere già
> dove sta la cosa, anche per una modifica di una riga. Il grafo è l'indice del
> repository: una query costa ~700 token e restituisce `file:riga`, chi chiama e chi
> è chiamato; un `grep` cieco costa una lettura, e una lettura intera resta nel
> contesto (e nella cache del modello) per tutta la sessione.
>
> Dal grafo si apre **solo il file e le righe indicate** — non il file intero, non i
> file vicini «per sicurezza». Il risparmio non è una preferenza: è la ragione per
> cui questa sezione esiste.

`graphify-out/graph.json` è la **mappa compilata del repository**: nodi (simboli,
concetti, documenti), archi (chi chiama chi, chi referenzia cosa) e comunità. È un
artefatto derivato: si rigenera, non si scrive a mano.

**Prima di aprire un file, prima di `rg`, prima di decidere dove mettere le mani:
interroga il grafo.**

```bash
graphify query "chi decide il layout della lista?"  # contesto ampio (BFS)
graphify query "..." --dfs --budget 1500            # traccia un percorso
graphify path "AppState" "UIRenderer"               # cammino più breve
graphify explain "GroupMenu"                        # un nodo e i suoi vicini
graphify affected "AppState" --depth 2              # chi tocca questo nodo
graphify god-nodes --top 10                         # i nodi di smistamento
```

`path` e `explain` vogliono l'**id completo** del nodo quando l'etichetta è
ambigua (`src_ui_renderer_uirenderer`, non `UIRenderer`): se rispondono
`Ambiguous:`, riprova con l'id che ti stampano.

Se il grafo manca o è vecchio — **su un clone nuovo manca sempre**, perché non è
versionato (§10):

```bash
# Primo avvio su un clone: ~6 secondi in tutto, nessuna chiave API.
graphify extract . --code-only      # AST dei soli sorgenti (~3,5 s su questo repo)
graphify cluster-only . --no-label  # comunità e report (~2,2 s)

# Dopo una modifica ai sorgenti, tutti i giorni:
graphify update .                   # solo i file cambiati, senza LLM
```

`cluster-only` **senza** `--no-label` prova a nominare le comunità con un modello
(nel test: una chiamata `claude -p`), quindi costa token e tempo per una comodità
che non serve a navigare. I nomi curati si rifanno quando servono davvero, con la
skill in chat — `/graphify`, o `/graphify --update` per documenti e immagini.

### Perché

- `src/main.cpp` è **4.816 righe**: scesa da 11.394 a **7.344** con lo spezzamento,
  **risalita** a 7.895 con l'editor della mappa dei tasti e il fissaggio, e scese di
  **3.079** con il pannello delle opzioni, i verbi della shell, il menu contestuale, le impostazioni e la navigazione — la curva di un
  file unico che accoglie ogni funzione nuova. Gli ultimi pezzi usciti sono
  l'**ispettore** intero (`src/view_inspector.*`, 1.053 righe), la **barra di stato**
  (`src/view_statusbar.*`) e i **menu a gruppi** (`src/view_menu.*`, 535): l'ispettore
  e il contenuto della cartella avevano appena riportato il file a 9.134, e spostarli
  subito è costato meno che spostarli dopo. Leggerlo per intero per rispondere a «dove sta
  X?» costa decine di migliaia di token e dà comunque una vista parziale.
- Una query restituisce **etichetta, file, riga e comunità** di ogni nodo: è
  l'indice di cui altrimenti ricostruiresti a mano con `rg`.
- Il grafo contiene già gli archi che una lettura per file non mostra: `AppState`
  tocca 154 nodi e collega 23 comunità, `WindowProc()` ne collega 51. Quelle
  relazioni non si vedono restando dentro un file.
- Un nodo ad alta **betweenness** (`AppState`, 0.277) dice dove passa tutto: è lì
  che una modifica costa di più, e va saputo **prima** di toccare.

### Quanto vale e quanto non vale

- Il grafo è una **derivazione**, non una fonte. Se un dettaglio conta per una
  decisione, **confermalo sul sorgente**: vedi §2, che vale anche qui.
- Il grafo è **vecchio appena modifichi un file**. Dopo una modifica sostanziale,
  `graphify update .`, o non fidarti delle risposte.
- Un file che l'estrattore non riesce a leggere **non è nel grafo**: l'assenza di un
  nodo è un'ipotesi, non la prova che il simbolo non esista. Caso noto: i typedef di
  puntatore a funzione con `APIENTRY` fermano il parser di `src/gl_loader.h` a riga
  52, quindi il file non produce **nessun** simbolo. Se il grafo dice che una
  funzione OpenGL non esiste, guarda `gl_loader.h` a mano.
- L'interprete usato da graphify è scritto in `graphify-out/.graphify_python`:
  serve per i comandi inline.

---

## 3. Lingua

- **Commenti, documentazione e messaggi di commit in italiano.**
- **Identificatori, nomi di file e stringhe di codice in inglese.**
- **I testi che si vedono si scrivono in due lingue**, italiano e inglese, con
  l'italiano per primo: `Tr("italiano", "english")` dove serve una, la tabella dei
  comandi con l'inglese (che è anche il nome della chiave) e `CommandLabelIt` per
  l'italiano. La lingua si sceglie dalla palette e vale per la sessione: `docs/06`
  vieta di scrivere un file di impostazioni di nascosto.
- **Anche i nomi che vengono dal riferimento si traducono**, e la scelta è
  dell'autore, che l'ha chiesta esplicitamente il 2026-10-14: le sezioni della barra
  laterale (`QUICK ACCESS`, `BOOKMARKS`, `STORAGE`, `PLACES`, `RECENTS`), il nome di
  `This PC`, i titoli delle colonne e le etichette dei gruppi si leggono nella lingua
  scelta. Prima si tenevano in inglese perché «sono i suoi nomi»; il riferimento però
  è un prodotto in inglese, e chi usa questo programma sceglie la lingua — un
  percorso che dice `This PC` accanto a una barra laterale che dice `Questo PC` è due
  nomi per la stessa cosa. **La chiave resta l'inglese**, e non è un dettaglio: è con
  quella che si ricorda se una sezione è chiusa o se un gruppo è collassato, così
  cambiare lingua non riapre quello che l'utente aveva chiuso. Le chiavi dello spec
  (`docs/03`) restano quelle e non si rinominano.
- Il registro è quello di chi spiega a un collega: diretto, concreto, senza
  entusiasmi e senza scuse. I commenti dicono **perché**, non cosa.

---

## 4. Comandi

`cmake` **non è nel PATH** della shell. Il percorso è:

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
```

**Costruire** (configurazione Visual Studio 17 2022, config RelWithDebInfo):

```bash
"$CMAKE" --build build --config RelWithDebInfo
```

**Lanciare** su una cartella:

```bash
./build/RelWithDebInfo/Bolt.exe "D:\una\cartella"
```

**Catturare** la finestra:

```bash
powershell -ExecutionPolicy Bypass -File tools/capture-window.ps1 \
    -ProcessName Bolt -OutputPath analysis/tmp/shot.png [-SettleMs 1000] [-ClientOnly]
```

Il default cattura la **finestra intera**. `-ClientOnly` esiste per i confronti al
pixel dell'area cliente. Non invertire questa scelta: vedi §7.

**Mandare tasti** a una finestra già aperta:

```bash
powershell -ExecutionPolicy Bypass -File tools/send-keys.ps1 -Action <azione> [-Text "..."]
```

Azioni: `text`, `backspace`, `enter`, `escape`, `delete`, `f2`, `down`, `up`,
`left`, `right`, `home`, `end`, `apps`, `alt+left`, `alt+right`, `ctrl+<lettera>`,
`ctrl+shift+<lettera>`, `shift+delete`.

**Mandare clic e rotella**, in coordinate **cliente** (così la prova si ripete
anche se la finestra si sposta):

```bash
powershell -ExecutionPolicy Bypass -File tools/send-mouse.ps1 -Action right -X 300 -Y 150
powershell -ExecutionPolicy Bypass -File tools/send-mouse.ps1 -Action left|-move|-wheel -X … -Y … [-Clicks n] [-Ctrl]
```

`-Ctrl` tiene premuto il modificatore per tutta l'azione, con `keybd_event`: serve a
`Ctrl`+rotella (la misura delle celle) e a `Ctrl`+clic, che sono gesti diversi dal
clic semplice e non si provano senza. `GetKeyState` non vede un modificatore
iniettato con un messaggio — vedi §5.

**Cercare una finestra per titolo**, per verificare l'effetto di un comando che è
un dialogo (proprietà, conferme, avvisi) senza catturare lo schermo e anche se la
finestra è dietro o invisibile:

```bash
powershell -ExecutionPolicy Bypass -File tools/find-window.ps1 -Needle <parola> [-Close]
```

Stampa classe e visibilità, non i titoli: un titolo può contenere il nome di un
file personale.

**Portare una finestra in primo piano**, prima di mandare tasti o clic — e solo se
sta sulla scrivania virtuale corrente, che è la condizione per non spostare la vista
di chi lavora (`AGENTS.md` §0):

```bash
powershell -ExecutionPolicy Bypass -File tools/focus-window.ps1 -ProcessName Bolt [-X 0 -Y 0 -Width 1240 -Height 760]
```

`SetForegroundWindow` da solo **non basta**: Windows lo rifiuta quando il primo piano
ce l'ha un'altra applicazione, e la chiamata riesce lo stesso — il sintomo è che i
tasti successivi vanno altrove e sembra che l'applicazione non risponda. Lo script
usa `AttachThreadInput`, ripristina la finestra se è ridotta, e **si rifiuta** se la
finestra non è sulla scrivania corrente. Vedi §5.

**Log di avvio**: `build/RelWithDebInfo/startup-error.log` (sovrascritto a ogni
avvio; assente significa nessun errore).

**Trascinare** (dentro e fuori): `tools/drag-mouse.ps1` preme, muove a passi,
**fotografa a metà gesto** e rilascia; `tools/window-rect.ps1` dice dove sta la
finestra adesso (`CW_USEDEFAULT` la sposta a ogni avvio, e le coordinate cliente
non si indovinano); `tools/capture-rect.ps1` salva un riquadro senza toccare
l'ordine delle finestre; `tools/explorer-side.ps1` apre Esplora risorse su una
cartella **del progetto** e lo mette di fianco.

**Contatori del motore nella barra di stato**: `BOLT_DEV_HUD=1` li rimette
(fotogrammi, quadrilateri, icone, lavori). Di default la barra mostra quello che
serve a chi usa il programma.

**Quale backend disegna**: `BOLT_BACKEND=opengl` o `d3d11` lo forza all'avvio; senza
la variabile si prova il Direct3D 11 e si ripiega sull'OpenGL se non parte. La riga
`--- backend:` in `bolt-diag.log` dice quale sta disegnando (`BOLT_DIAG=1`).

**Log dei verbi della shell**: `BOLT_DIAG=1` fa scrivere a ogni
interrogazione di `IContextMenu` le voci trovate, i verbi, quelle saltate e il
tempo impiegato, in `bolt-diag.log` accanto all'eseguibile. Spento di default,
come il registro di avvio: è lo strumento che ha reso leggibile cosa la shell
offre su **questa** macchina, e quello che serve per i sottomenu.

**Le shader non richiedono ricompilazione.** `REA_SHADER_DIR` punta alla sorgente,
quindi `src/shaders/*` si modificano a caldo.

**Una build diagnostica non si lascia in giro.** Per cacciare una caduta rara serve
AddressSanitizer, che vuole una cartella di build **separata** (`build/asan`) e il
runtime dinamico — il CRT statico del progetto non e' compatibile. Quell'eseguibile
pretende `clang_rt.asan_dynamic-x86_64.dll`: senza, Windows mostra
`Bolt.exe - Errore di sistema` a chi lo lancia, e sembra un guasto del programma. Si
costruisce, si usa, si cancella — e `CMakeLists.txt` torna com'era. L'eseguibile buono
si controlla con `dumpbin -dependents`: non deve nominare `clang_rt`. Vedi `docs/05` §4.

---

## 5. Trappole già pagate — non ripagarle

Costano tempo, sembrano bug dell'applicazione e non lo sono. Vedi anche §5 di
`docs/05-stato-e-prossimi-passi.md`.

### Cattura e finestre

- **La finestra deve essere in primo piano**, o la cattura riprende ciò che sta
  sopra. `SetWindowPos(h, HWND_TOPMOST, …)` funziona sempre — **e va tolto alla
  fine**, altrimenti le finestre di dialogo finiscono dietro e sembrano non
  essersi aperte.
- **Una cattura si legge da un pixel, non dalla miniatura.** Un colore, un bordo di
  un pixel, un cursore: l'anteprima che si guarda non è la prova, il pixel lo è. Due
  volte in una sessione una cattura **scura** è stata descritta come chiara, perché
  sembrava di doverlo vedere.
- **Due catture si confrontano solo a parità di stato.** Un'opzione accesa in un giro
  e spenta nell'altro — o un filtro, o un'altra cartella — spiegano una differenza che
  si finisce per attribuire a ciò che si sta provando. Prima si guarda cos'**altro**
  differisce fra i due giri.
- **Una caduta rara non si attribuisce con tre tentativi.** Tre avvii per parte non
  distinguono una probabilità del 40% da una del 10%: una diagnosi fatta su quel
  campione manda a cercare il difetto nel posto sbagliato. Per attribuire un difetto
  intermittente servono campioni grandi, e per trovarne la causa una **pila** (un
  debugger, AddressSanitizer); se sotto lo strumento non si riproduce, si dichiara
  coi numeri che si hanno.
- **Un'applicazione che sembra congelata può essere dentro un ciclo di messaggi
  modale.** Due catture identiche a distanza di secondi distinguono "fermo" da
  "lento"; `SendMessageTimeout` dice se risponde, ma **non** se sta lavorando.
  Quando l'app non ridisegna e risponde ai messaggi, **enumera le finestre del
  processo** e leggi i titoli: la finestra che blocca può essere invisibile.
  Con `tools/find-window.ps1`, e ricordando che il titolo cercato può comparire
  anche nella finestra dell'applicazione, che allora va risparmiata dal
  `WM_CLOSE` di pulizia.

### Input sintetico

- **Scegliere una voce di menu col puntatore è un bersaglio mobile.** Il menu
  contestuale non è alto uguale a ogni apertura — dalla seconda arrivano le
  estensioni moderne di Windows 11 — quindi le righe sotto il puntatore scendono.
  Per una voce precisa: aprire il menu e usare la tastiera (`Fine`, poi `giù`).
- **`SendKeys` non dà il focus a una finestra OpenGL.** Serve `keybd_event`.
- **`keybd_event` va preceduto da `SetForegroundWindow`**, o i tasti vanno
  altrove. Un falso negativo da mancanza di focus sembra un bug dell'app: **leggi
  il codice prima di correggerlo**.
- **`SetForegroundWindow` da solo non basta quando il primo piano ce l'ha un'altra
  applicazione**: Windows lo rifiuta, la chiamata riesce lo stesso, e i tasti e i clic
  successivi vanno da un'altra parte. Serve `AttachThreadInput` al thread che possiede
  il primo piano: è quello che fa `tools/focus-window.ps1`, che **si rifiuta** anche di
  agire su una finestra che non sta sulla scrivania virtuale corrente (`§0`). È così che
  il 2026-10-17 una prova di trascinamento è sembrata «non riproducibile in questo
  banco» — e la conclusione era sbagliata: il banco aveva il primo piano da un'altra
  parte.
- **Una finestra può restare `topmost`** dopo una prova: `SetWindowPos` con
  `HWND_TOPMOST` non si toglie da solo, e le finestre di dialogo finiscono dietro. Si
  toglie alla fine, o si chiude il processo di prova.
- **Su una finestra non attiva il primo clic la attiva e non fa altro.** Un
  trascinamento che comincia da lì non comincia, e sembra che la funzione di
  trascinamento non funzioni: è successo per due tentativi di fila. Prima di un
  gesto, un clic su un punto neutro (il fondo della lista).
- **Quello che vedi non è detto sia quello che credi.** In una prova un clic su una
  riga non ha selezionato niente per due volte: l'applicazione era su **un'altra
  cartella** con un **filtro** che non corrispondeva a niente, quindi la lista era
  vuota e il clic cadeva nel vuoto. Prima di misurare coordinate, guarda il percorso
  e il filtro: sono due righe di cattura, e risparmiano mezz'ora di diagnosi.
- **`GetKeyState` non vede un modificatore iniettato con `SendMessage`.** Per i
  tasti con `Ctrl`/`Shift` serve `keybd_event`; per quelli senza, `SendMessage`
  con `WM_KEYDOWN`/`WM_CHAR` è più affidabile.
- **`Alt`+`←`/`→` arrivano come `WM_SYSKEYDOWN`**, non `WM_KEYDOWN`: la finestra ha
  un menu di sistema.
- **`Add-Type` con una direttiva `using` malformata fallisce in silenzio** e i
  tasti non partono mai: sembra che l'applicazione ignori l'input. Per questo
  esiste `tools/send-keys.ps1`: uno script su file è l'unico modo affidabile di
  ripetere una prova.
- **Se `window-rect.ps1` risponde `0x0`, la finestra non c'è (ancora).** Il
  rettangolo calcolato da quel valore è l'angolo dello schermo, e `capture-rect.ps1`
  salva quello: è successo, e la cattura è stata buttata senza guardarla. La
  dimensione si controlla **prima** di catturare, non dopo.
- **`send-mouse.ps1` vuole coordinate CLIENTE, `drag-mouse.ps1` coordinate
  SCHERMO**, e la ragione è scritta in testa a ciascuno: un clic si ripete anche se
  la finestra si sposta, un trascinamento attraversa due finestre e le coordinate
  cliente dell'una non vogliono dire niente per l'altra. Scambiarle non dà un
  errore: **dà un clic da un'altra parte**, che è peggio, perché sembra che
  l'applicazione non risponda. Un file di prova è finito sul Desktop di chi lavora
  proprio così (vedi `docs/05`, §0).

### Modifiche in massa

- **Un taglio meccanico che cerca una firma trova anche la sua dichiarazione.** Per
portare una funzione fuori da `main.cpp` si cercava la firma — `std::wstring
ParentOf(const std::wstring& path)` — e si tagliava fino alla graffa di chiusura. Ma
la stessa firma compariva **prima**, come dichiarazione anticipata in cima al file,
e il taglio e' partito da li': dalla dichiarazione fino alla graffa della prima
funzione che seguiva, mangiandosi `SettingsPath` e una dichiarazione di
`NowSeconds`. Il conteggio delle righe sembrava plausibile (19 righe per una funzione
da 8: il numero che deve insospettire c'e' sempre, e va guardato). Se n'e' accorto il
**compilatore**, non l'occhio: da allora il taglio pretende che la riga dopo la firma
sia **esattamente** `{`, che e' cio' che distingue una definizione da una
dichiarazione. Vale la regola di sopra, applicata ai tagli: dopo una riscrittura
meccanica si legge cio' che e' stato tolto, non solo cio' che e' rimasto.

**E il taglio va scritto in modo che il resto non sparisca.** Nello stesso lavoro
un secondo script faceva `t = prima_del_taglio` invece di `t = prima + dopo`: la
seconda ricerca partiva su un file troncato e non trovava piu' niente. Li' l'errore
si e' fatto sentire subito (un'eccezione, nessun file scritto), ma la forma e' la
peggiore: un taglio che perde la coda **in silenzio** produce un file che compila
meno, e meta' del contenuto non c'e' piu'.

**Un commento in coda alla riga inganna la fine dell'istruzione.** La stessa specie
di taglio, un'altra veste: cercando la fine di un'istruzione con «la riga finisce con
`;`», una riga come

```cpp
    DiagLog("...");   // TEMPORANEO: diagnosi
```

non finisce con `;` — finisce con la parola `diagnosi`. Il taglio ha continuato a
mangiare righe finche' non ne ha trovata una che finisse con un punto e virgola, e
quella era **la riga di codice dopo**, in un'altra funzione: ne sono spariti una
`const float scale = layout.scale;`, un'intera definizione (`HitTest`), una funzione
(`OpenPropertiesPanel`) e qualche chiusura di graffa, sparse in sei funzioni. Il
taglio giusto guarda la **struttura** (le graffe prima e dopo), non l'ultimo
carattere della riga; e dopo un taglio meccanico si confrontano le **funzioni** —
quante righe ha ognuna prima e dopo — invece di leggere a campione un diff da
duemila righe. Se ne sono accorti il compilatore (`HitTest` non definito) e il
conteggio per funzione (`OpenPropertiesPanel` sparito): due strumenti, non l'occhio.

**Le riscritture massive di chiamate annidate duplicano il testo.** Una regex
  che trova sia la chiamata esterna sia quella annidata produce intervalli
  **sovrapposti** e incolla il secondo sopra il primo. Il compilatore non se ne
  accorge subito. **Rileggi sempre il diff riga per riga** dopo una riscrittura
  meccanica: ha trovato errori che nessun test visivo avrebbe attribuito alla
  causa giusta.

### Testo e nomi

- **Un nome di file si prova anche con un accento dentro.** L'atlante dei glifi
  copre ASCII, latino-1 e pochi simboli; quello che sta fuori si disegna come un
  **buco**, e un taglio a metà di una sequenza UTF-8 lascia in fondo al nome un
  carattere che non c'è. Con l'atlante solo-ASCII, `perché.txt` appariva come
  `perch .txt`, e nessuna prova lo mostrava perché i nomi di prova erano tutti
  senza accenti. Vedi `docs/02` §7.29.
- **Un nome di spazio dei nomi sbagliato non dà errore: dà una cartella vuota.** È
  la forma di guasto peggiore, perché sembra che non ci sia niente da mostrare.
  Prima di usarlo lo si verifica, e una sezione vuota non si disegna: `docs/02`
  §7.30.
- **Un carattere non-ASCII si scrive nel codice solo se sta nell'insieme
  rasterizzato.** Fuori da lì si disegna come un **buco**, e un buco non fa rumore:
  è così che un trattino lungo è stato invisibile in una riga dell'inspector
  (§7.32). Se serve un carattere nuovo, si aggiunge all'atlante **e** si guarda a
  schermo.
- **Una domanda senza senso riceve comunque una risposta.** Chiedere le statistiche
  ricorsive di un volume dà zero byte, zero cartelle e zero file, e l'inspector lo
  scriveva come se fosse un fatto: un disco da 952 GB dichiarato vuoto (§7.31). Non
  basta che la funzione risponda: bisogna che la domanda sia quella giusta.

### Operazioni su file

- **`IFileOperation` non ha un metodo per fermarsi.** Non esiste `Abort`: chi annulla
  e' il **sink**, che risponde con un codice d'errore. Un errore da `Pre*Item` annulla
  l'operazione **e tutte quelle in coda** (e' nel documento di quel metodo);
  `UpdateProgress` passa di li' anche a meta' di un file solo, ed e' l'unico punto da
  cui fermare una copia da 5 GB prima della sua fine. Verificato: annullata a meta' di
  un file da 4 GB, la shell **cancella da sola il file parziale** che aveva creato.
- **I "punti" di `UpdateProgress` non sono byte.** Misurati: un file da 4 GB sono
  **131074 punti** (32 KB l'uno), un file da 23 byte sono **2 punti**. La frazione e'
  una frazione — una buona stima del tempo — e i **byte** vogliono un denominatore
  misurato da noi. Si legge in `bolt-diag.log` (`BOLT_DIAG=1`), che scrive byte
  misurati e punti dichiarati **nella stessa riga**: cosi' la differenza si vede
  invece di supporla.
- **Un'operazione lunga su un thread di lavoro rende possibile la seconda
  operazione.** Sostituire quella in corso vuol dire annullarla **senza dirlo**, e
  per una caduta e' peggio che per un comando: chi ha mollato il tasto legge dalla
  destinazione se l'operazione e' riuscita, e su `DROPEFFECT_MOVE` cancella gli
  originali. Una caduta rifiutata deve dichiarare `DROPEFFECT_NONE`.
- **La finestra di Bolt si sposta a ogni avvio.** `send-keys.ps1` e `send-mouse.ps1`
  vogliono coordinate **cliente** e le convertono da soli; `capture-rect.ps1` e
  `drag-mouse.ps1` vogliono coordinate **schermo**, e l'origine va **rimisurata dopo
  ogni avvio** con `window-rect.ps1`. Con l'origine vecchia la cattura esce su un
  altro riquadro, e una cattura sbagliata **sembra** una prova.

### Test

- **Usa una cartella usa e getta** dentro il progetto. Le operazioni su file
  cancellano davvero. **Mai provare su file personali** — vedi §0, che vale senza
  eccezioni.
- **Un file grande per provare il conto e l'annullamento** si crea nella sandbox in
  un terzo di secondo — `fsutil file createnew analysis\tmp\sandbox\big.bin
  4294967296` — e si cancella prima di lasciare la sessione. Non serve un file vero:
  quello che si guarda e' il conto, non il contenuto.

---

## 6. Principi di codice

- **I numeri derivati battono i numeri fissati a occhio.** Un'altezza di cella
  derivata da icona più etichetta non può entrare in disaccordo con le sue parti;
  una scelta a mano sì, e ogni volta che una delle due parti cambia.
- **Niente astrazioni prima del secondo utilizzatore.** Il componente a gruppi è
  stato estratto quando è arrivato GoTo, non prima.
- **Un valore derivato al posto di uno indovinato.** Se la shell ti dice il nome
  che ha scelto, non provare i suffissi `(2)`, `(3)`, `(4)` in un ciclo.
- **I limiti si dichiarano.** Nell'interfaccia o nella roadmap, mai nascosti. Un
  controllo che dichiara qualcosa che la vista non sta facendo è peggio di un
  controllo assente.
- **Un'azione che fallisce in silenzio è peggio di una che non parte.** Ogni esito
  va riportato.
- **Build senza warning** (`/W4` con MSVC). Un warning nuovo è un difetto nuovo.
- **Un campo scritto e mai letto è un difetto silenzioso.** Cerca i *lettori*, non
  gli *scrittori*.
- **Quando manca una funzione, chiediti «lo spec la elencava?»**, non «l'avevo
  prevista?». `docs/03-spec-funzionale.md` è la lista di ciò che manca;
  `docs/04-roadmap.md` è solo l'ordine in cui si pensava di farlo.

---

## 7. Il difetto delle due barre del titolo — leggerlo prima di toccare la finestra

Per tutta la durata di M6 l'applicazione ha mostrato **due** barre del titolo, e
nessuna verifica l'aveva mai visto, per una ragione sola: `capture-window.ps1`
leggeva `GetClientRect`, e la barra di sistema vive **fuori** dall'area cliente. Il
banco di prova ritagliava via esattamente la regione del difetto.

La causa: `case WM_NCCALCSIZE: if (wparam == TRUE) { … return 0; }` — il messaggio
arriva con `wParam` a **`FALSE`**, la condizione non entrava mai, il caso cadeva su
`DefWindowProc`, che ricalcola l'area non-cliente **dallo stile** (che contiene
`WS_CAPTION`) e rimetteva la didascalia.

Togliere l'area non-cliente toglie anche ombra e angoli arrotondati: servono
`DwmExtendFrameIntoClientArea` di un pixel per lato,
`DWMWA_USE_IMMERSIVE_DARK_MODE` e `DWMWA_WINDOW_CORNER_PREFERENCE`.

Tre regole che ne derivano:

1. **La cattura mostra la finestra intera**, non l'area cliente.
2. **Un valore incerto si logga, non si deduce.** Una riga di registro ha risolto
   quello che dieci minuti di ragionamento stavano aggirando.
3. **Dopo aver corretto qualcosa, rimisura il bordo attorno alla correzione**: è la
   parte che non si guarda mai, ed è dove si nasconde il difetto successivo.

---

## 8. Architettura in breve

**Un solo array di texture** (1024 px, **6 strati**), **un quad batcher**, **cinque
draw call per frame**: quattro in piu' sono il prezzo dei due ritagli (lista e
visualizzatore di testo), perche' il batch va chiuso e riaperto attorno al
`glScissor`, o le celle della griglia e le righe di testo escono dal riquadro e
finiscono sopra le barre (`docs/02` §7.36 e §7.37).

**Debito dichiarato:** il posto giusto per un ritaglio e' il **quad**, non la
chiamata di disegno — un rettangolo di ritaglio per primitiva, valutato nello
shader, riporterebbe il fotogramma a un disegno solo e renderebbe il ritaglio una
proprieta' di chi disegna invece che dell'ordine in cui si disegna. Finche' non
c'e', ogni area ritagliata costa due disegni.

| strato | contenuto |
|---|---|
| 0 | glifi del corpo, 15 px |
| 1 | icone di sistema |
| 2 | anteprime dei file (cella 128, cache 64) |
| 3 | glifi secondari: i **dettagli** (intestazioni, ispettore, barra di stato), 4 px sotto il corpo |
| 4 | glifi del testo, 14 px a spaziatura fissa (Consolas) |
| 5 | l'immagine dell'anteprima grande, fino a 1024 px |

Le forme sono disegnate da uno shader **SDF** (`src/shaders/ui.frag`), quindi ogni
rettangolo ha bordi continui a qualunque dimensione. Anche le **linguette** delle
sezioni sono una forma — un triangolo — e non il carattere `v`: un carattere cambia
col carattere scelto e non e' un elemento.

### File

| file | responsabilità |
|---|---|
| `src/main.cpp` | finestra Win32, navigazione, layout, input, disegno; sceglie e possiede il backend (`BOLT_BACKEND`, fallback). **Lo spezzamento è in corso: 4.816 righe, erano 11.394** (scesa a 7.344, risalita a 7.895, scesa di 3.079 il 2026-10-18: vedi §2-bis e `docs/05` §2) |
| `src/theme.*` | i **ruoli di colore** che l'interfaccia legge e le **due tavolozze** (chiara e scura), piu' la preferenza del sistema (`AppsUseLightTheme`). I 35 token dello spec che hanno un lettore stanno qui; quelli che non ce l'hanno sono elencati in `theme.h`, invece di essere campi scritti e mai letti |
| `src/app_state.h` | lo stato della finestra e i tipi che piu' moduli condividono: il **confine** che prima non esisteva (`docs/05` §2) |
| `src/app_ui.*` | i supporti che piu' di un modulo usa: UTF-8/UTF-16, nomi di percorso, numeri in lettere, date, `FitText`, `NowSeconds` |
| `src/view_sidebar.*` | la barra laterale: costruzione delle voci e disegno, primo modulo di **vista** uscito da `main.cpp`. Le sezioni sono quattro piu' i recenti, e in `ACCESSO RAPIDO` le righe **nostre** (`AppState::pins`, in memoria) stanno in cima a quelle della shell con la stella che le distingue |
| `src/tabs.*` | l'albero dei tab: identita', genitore, ordine di lettura. La parte **pura** del modello; chi apre e chiude tocca la navigazione e resta in `main.cpp` |
| `src/view_titlebar.*` | la striscia dei tab, il menu e i tre pulsanti di finestra; la **geometria** dei pulsanti resta qui e l'input la legge dai bersagli registrati |
| `src/view_list.*` | la **lista con le colonne**: ordinamento, filtro, gruppi e viste (dalle voci alle righe, `RebuildView`), la geometria della griglia e delle righe (`ComputeViewMetrics`, `ItemRect`) e il disegno. Le domande su una voce — cursore, selezione, percorso, icona — stanno qui, cosi' l'input e il disegno non se le ricalcolano in due modi |
| `src/view_inspector.*` | il pannello di destra **intero**: le righe dei metadati (`BuildInspectorRows`, le stesse che usa il pannello delle proprieta'), l'immagine grande decodificata, il visualizzatore di testo e il contenuto della cartella scelta |
| `src/view_statusbar.*` | la barra di stato e la riga in fondo: l'esito di quello che si e' appena fatto, i numeri, e i selettori di vista/misura/ingrandimento. Registra i loro rettangoli come bersagli e **non** risponde ai clic: quello lo fa l'input |
| `src/view_options.*` | il **pannello delle opzioni** (`Ctrl+,`): la costruzione dell'elenco delle preferenze, il passaggio dello stato alla riga e ritorno (`RefreshOptionItem`/`ApplyOptionFromItem`, le due direzioni di una verità sola), i controlli tipizzati — testo, numero, interruttore, scelta — la modifica in linea, e i tasti e i clic del pannello. Uscito da `main.cpp` il 2026-10-18 |
| `src/shell_menu.*` | i **verbi della shell**: leggere `IContextMenu` dentro le nostre voci, la banda di id riservata (`kShellCmdFirst..Last`), le etichette ridotte ad ASCII, la cache per non interrogare la shell a ogni tasto destro, e il rilascio degli oggetti COM. Il *disegno* del menu contestuale e l'esecuzione delle voci restano in `main.cpp` |
| `src/view_context_menu.*` | il **menu contestuale** come superficie: i livelli, la geometria delle righe, lo scorrimento, il disegno, il puntatore, il clic, i tasti, e le azioni del menu (aprire un sottomenu, eseguire una voce, le proprietà). I verbi **non** li legge: quelli sono `shell_menu.*` |
| `src/navigation.*` | la **navigazione** e i **tab**: leggere una cartella (`LoadDirectory`, `ReadDirectory`, e le voci di spazio dei nomi), la storia avanti/indietro, e l'albero dei tab — aprirne, attivarlo, chiuderlo, chiudere un ramo, e accogliere un tab trascinato sopra un altro |
| `src/input_targets.*` | il **registro dei bersagli** (`HitTest`): chi disegna registra un rettangolo e un tipo, chi riceve il clic chiede qui cosa c'è sotto il puntatore invece di ricalcolare le geometrie |
| `src/settings.*` | le **preferenze** su disco: `%APPDATA%\Bolt\settings.ini`, una riga per preferenza, testo semplice cancellabile a mano, e il "ricorda" che dice quando non riesce. Il file non contiene nomi di file né cartelle visitate (`docs/06`) |
| `src/app_actions.h` | **la cucitura dello spezzamento**: le azioni che vivono ancora in `main.cpp` e che i moduli chiamano (l'editor della mappa dei tasti, la chiusura del menu dei fissaggi, i comandi che il menu contestuale chiama), ognuna col modulo che dovrebbe avere accanto. Una lista che deve **accorciarsi** |
| `src/view_menu.*` | il componente a gruppi — palette, GoTo, opzioni, valori di una colonna — con lo stato, il filtro, la geometria e il disegno. `GroupMenuClick`/`GroupMenuKeyDown` restituiscono un `MenuAction`: **cosa significa** una voce scelta lo sa chi ha costruito il menu |
| `src/keymap.*` | **quale tasto fa quale comando**: una tabella `comando -> tasto` con i tasti di partenza, la ricerca (`CommandFor`), la scrittura (`LabelFor`), la rimappatura (`Rebind`, `Clear`, `Reset`) e il nome di un tasto (`Label`, che per quelli che non hanno un nome scritto a mano lo chiede a Windows). E' la fonte unica: l'input la interroga invece di conoscere i tasti, e le righe di fondo dei menu si scrivono da lei. Chi la **cambia** sta in `main.cpp`: la riga `Key Map` fra le opzioni (e `CmdEditKeyMap` dalla palette) apre l'elenco dei comandi col tasto accanto, e la cattura del tasto nuovo |
| `src/ui_renderer.*` | il backend **OpenGL** dietro `IRenderer`: quad batcher, una draw call, catena di mipmap |
| `src/d3d11_renderer.*` | il backend **Direct3D 11** della stessa interfaccia: stessi 30 float per vertice, stessi shader in HLSL, swap chain che segue la finestra |
| `src/font_atlas.*` | atlante di glifi GDI → strato dell'array; ClearType ridotto a copertura in scala di grigi, con gutter fra le celle |
| `src/shell_icons.*` | icone di sistema → strato 1 |
| `src/drag_drop.*` | trascinamento OLE: `IDropTarget` sulla finestra, `IDropSource` per portare fuori |
| `src/diag.*` | registro diagnostico (`BOLT_DIAG=1` → `bolt-diag.log`) |
| `src/image_preview.*` | decodifica di un'immagine via WIC, ridimensionata per l'anteprima grande |
| `src/text_view.*` | decodifica di un file di testo: codifica, righe, terminatori, rifiuto dei binari |
| `src/shell_location.*` | le cartelle che non sono percorsi (cestino, Questo PC): enumerazione, nomi, PIDL |
| `src/ntfs_attributes.*` | gli attributi del record NTFS di un file: volume raw in sola lettura, `FSCTL_GET_NTFS_FILE_RECORD`, la *update sequence array*, i nomi della tabella `AT_*`. La parte che interpreta i byte e' separata da quella che fa I/O, cosi' si prova anche senza privilegi |
| `src/shell_new.*` | i modelli del menu "Nuovo": chiavi `ShellNew` del registro, etichette risolte con `SHLoadIndirectString` |
| `src/shell_sendto.*` | le destinazioni di "Invia a": la cartella SendTo, letta a ogni apertura |
| `src/shell_preview.*` | anteprime via `IShellItemImageFactory` → strato 2, worker MTA |
| `src/dir_stats.*` | statistiche ricorsive: job, profili di volume, budget di tempo riprendibile; l'allocato si legge a blocchi per cartella |
| `src/dir_watch.*` | sorveglianza della cartella aperta (`ReadDirectoryChangesW`), per aggiornare i numeri senza `F5`; limite: non e' ricorsiva |
| `src/file_ops.*` | copia, sposta, rinomina, cestino via `IFileOperation`, e le **operazioni lunghe** — copia, spostamento, cestino — su un **thread di lavoro** (`FileOps::Job`) col conto dei byte, la voce in corso e l'annullamento che passa dal sink; clipboard `CF_HDROP` |
| `src/language.*` | quale delle due lingue si mostra, con `Tr`/`TrW` e i conteggi (`Copiato 1 elemento` / `Copiati 3 elementi`); le coppie di stringhe restano dove si usano |
| `src/gl_loader.*` | entry point OpenGL 3.3 caricate a runtime |
| `src/bolt.rc`, `src/resource.h` | icona incorporata, da `brand/bolt.ico` |
| `tools/` | cattura, input sintetico, `handle-count.ps1` (costo o perdita) |
| `brand/` | identità visiva, brief del logo, icona |

### Dove sta lo stato del lavoro

- `docs/04-roadmap.md` — le milestone, con le caselle spuntate e i difetti aperti.
- `docs/05-stato-e-prossimi-passi.md` — dove si è arrivati, cosa resta, le trappole.
- `docs/02-architettura-ricostruita.md` — l'architettura e le **cinquantasette** voci
  del registro dei difetti, §7: trovate guardando l'applicazione, con sintomo, causa e
  correzione (tre sono aperte e lo dicono nel titolo).
- `docs/03-spec-funzionale.md` — la spec ricostruita dall'originale: 1124 chiavi,
  319 comandi, 35 token di tema, 23 colonne.
- `graphify-out/graph.json` — la **mappa** del repository (nodi, archi, comunità):
  si interroga invece di leggere i file. Vedi §2-bis.

---

## 9. Cosa non fare mai

- **Non toccare file personali, non salvare informazioni personali.** Vedi §0:
  vale più di tutto il resto scritto in questo file.
- **Non committare `analysis/`.** Contiene le copie del binario originale, gli
  snapshot, le stringhe estratte e le funzioni decompilate: pubblicarlo
  significherebbe ridistribuire un prodotto commerciale. È già in `.gitignore` —
  non togliere quella riga.
- **Non usare `SHFileOperation`**: è deprecata. Le operazioni su file passano da
  `IFileOperation`.
- **Non rinominare i riferimenti a File Pilot** che designano il prodotto originale.
- **Non dichiarare fatto ciò che non hai visto.** Vedi §2.
- **Non lasciare warning** nella build.
- **Non aggiungere dipendenze** senza una ragione che stia in piedi da sola.
- **Non leggere il sorgente per orientarti.** La mappa è `graphify-out/graph.json`:
  il primo passo di ogni richiesta sul codice è `graphify query` / `explain` /
  `path` / `affected`, e dal grafo si aprono **solo** le righe indicate. Niente
  `ls -R`, niente `grep` esplorativo, niente file interi «per capire dove mettere le
  mani»: una query costa poco, una lettura costa migliaia di token che restano nel
  contesto per tutta la sessione. Vedi §2-bis.
- **Non aprire nuove sessioni di refactoring largo** senza spazio per riverificare
  tutto: `main.cpp` a 4.816 righe è un problema noto e in corso di soluzione, non una scoperta.

---

## 10. Git

- L'ambiente salva con **commit automatici** ("checkpoint: auto-save at turn N") a
  ogni turno; i commit scritti a mano si sovrappongono a quelli e spesso non
  servono. Non è un errore se `git commit` dice "nothing to commit".
- Ramo di lavoro: `main`, con remoto `origin`.
- `.gitignore` esclude `build/`, `analysis/`, `sessione.html`, `graphify-out/`,
  `out/`, `.vs/`, `*.user`, `*.suo`, `*.log`.
- **`graphify-out/` non si versiona.** Contiene la mappa (§2-bis), che è derivata:
  si rigenera dai sorgenti senza chiavi API. In più `.graphify_python` porta dentro
  il percorso assoluto dell'interprete della macchina che l'ha generata, e la cache
  cambia a ogni run. Le copie già entrate nella storia non si tolgono con una riga
  di `.gitignore`.
- Il progetto **non ha una convenzione di branch**: si lavora su `main`.
