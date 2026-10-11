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
testo, un CSV e una PNG generata. `analysis/` è esclusa dal controllo di versione,
quindi niente di tutto questo finisce nel repository.

Mai usare il Desktop, i Download o le Immagini di chi lavora, **nemmeno come
"cartella di esempio"** per una cattura.

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

## 3. Lingua

- **Commenti, documentazione e messaggi di commit in italiano.**
- **Identificatori, nomi di file e stringhe di codice in inglese.**
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
`alt+left`, `alt+right`, `ctrl+<lettera>`, `ctrl+shift+<lettera>`, `shift+delete`.

**Log di avvio**: `build/RelWithDebInfo/startup-error.log` (sovrascritto a ogni
avvio; assente significa nessun errore).

**Le shader non richiedono ricompilazione.** `REA_SHADER_DIR` punta alla sorgente,
quindi `src/shaders/*` si modificano a caldo.

---

## 5. Trappole già pagate — non ripagarle

Costano tempo, sembrano bug dell'applicazione e non lo sono. Vedi anche §5 di
`docs/05-stato-e-prossimi-passi.md`.

### Cattura e finestre

- **La finestra deve essere in primo piano**, o la cattura riprende ciò che sta
  sopra. `SetWindowPos(h, HWND_TOPMOST, …)` funziona sempre — **e va tolto alla
  fine**, altrimenti le finestre di dialogo finiscono dietro e sembrano non
  essersi aperte.
- **Un'applicazione che sembra congelata può essere dentro un ciclo di messaggi
  modale.** Due catture identiche a distanza di secondi distinguono "fermo" da
  "lento"; `SendMessageTimeout` dice se risponde, ma **non** se sta lavorando.
  Quando l'app non ridisegna e risponde ai messaggi, **enumera le finestre del
  processo** e leggi i titoli: la finestra che blocca può essere invisibile.

### Input sintetico

- **`SendKeys` non dà il focus a una finestra OpenGL.** Serve `keybd_event`.
- **`keybd_event` va preceduto da `SetForegroundWindow`**, o i tasti vanno
  altrove. Un falso negativo da mancanza di focus sembra un bug dell'app: **leggi
  il codice prima di correggerlo**.
- **`GetKeyState` non vede un modificatore iniettato con `SendMessage`.** Per i
  tasti con `Ctrl`/`Shift` serve `keybd_event`; per quelli senza, `SendMessage`
  con `WM_KEYDOWN`/`WM_CHAR` è più affidabile.
- **`Alt`+`←`/`→` arrivano come `WM_SYSKEYDOWN`**, non `WM_KEYDOWN`: la finestra ha
  un menu di sistema.
- **`Add-Type` con una direttiva `using` malformata fallisce in silenzio** e i
  tasti non partono mai: sembra che l'applicazione ignori l'input. Per questo
  esiste `tools/send-keys.ps1`: uno script su file è l'unico modo affidabile di
  ripetere una prova.

### Modifiche in massa

- **Le riscritture massive di chiamate annidate duplicano il testo.** Una regex
  che trova sia la chiamata esterna sia quella annidata produce intervalli
  **sovrapposti** e incolla il secondo sopra il primo. Il compilatore non se ne
  accorge subito. **Rileggi sempre il diff riga per riga** dopo una riscrittura
  meccanica: ha trovato errori che nessun test visivo avrebbe attribuito alla
  causa giusta.

### Test

- **Usa una cartella usa e getta** dentro il progetto. Le operazioni su file
  cancellano davvero. **Mai provare su file personali** — vedi §0, che vale senza
  eccezioni.

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

**Un solo array di texture** (1024 px, 4 strati), **un quad batcher**, **una draw
call per frame**.

| strato | contenuto |
|---|---|
| 0 | glifi del corpo, 15 px |
| 1 | icone di sistema |
| 2 | anteprime dei file (cella 128, cache 64) |
| 3 | glifi secondari, 13 px |

Le forme sono disegnate da uno shader **SDF** (`src/shaders/ui.frag`), quindi ogni
rettangolo ha bordi continui a qualunque dimensione.

### File

| file | responsabilità |
|---|---|
| `src/main.cpp` | finestra Win32, contesto GL, navigazione, layout, input, disegno. **~4.300 righe: è il collo di bottiglia dichiarato.** |
| `src/ui_renderer.*` | quad batcher, una draw call, catena di mipmap |
| `src/font_atlas.*` | atlante di glifi GDI → strato dell'array; ClearType ridotto a copertura in scala di grigi, con gutter fra le celle |
| `src/shell_icons.*` | icone di sistema → strato 1 |
| `src/shell_preview.*` | anteprime via `IShellItemImageFactory` → strato 2, worker MTA |
| `src/dir_stats.*` | statistiche ricorsive: job, profili di volume, budget di tempo riprendibile |
| `src/file_ops.*` | copia, sposta, rinomina, cestino via `IFileOperation`; clipboard `CF_HDROP` |
| `src/gl_loader.*` | entry point OpenGL 3.3 caricate a runtime |
| `src/bolt.rc`, `src/resource.h` | icona incorporata, da `brand/bolt.ico` |
| `tools/` | cattura e input sintetico |
| `brand/` | identità visiva, brief del logo, icona |

### Dove sta lo stato del lavoro

- `docs/04-roadmap.md` — le milestone, con le caselle spuntate e i difetti aperti.
- `docs/05-stato-e-prossimi-passi.md` — dove si è arrivati, cosa resta, le trappole.
- `docs/02-architettura-ricostruita.md` — l'architettura e i **quattordici** difetti
  trovati e risolti, §7.
- `docs/03-spec-funzionale.md` — la spec ricostruita dall'originale: 1124 chiavi,
  319 comandi, 35 token di tema, 23 colonne.

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
- **Non aprire nuove sessioni di refactoring largo** senza spazio per riverificare
  tutto: `main.cpp` a 4.300 righe è un problema noto e rimandato, non una scoperta.

---

## 10. Git

- L'ambiente salva con **commit automatici** ("checkpoint: auto-save at turn N") a
  ogni turno; i commit scritti a mano si sovrappongono a quelli e spesso non
  servono. Non è un errore se `git commit` dice "nothing to commit".
- Ramo di lavoro: `main`, con remoto `origin`.
- `.gitignore` esclude `build/`, `analysis/`, `sessione.html`, `out/`, `.vs/`,
  `*.user`, `*.suo`, `*.log`.
- Il progetto **non ha una convenzione di branch**: si lavora su `main`.
