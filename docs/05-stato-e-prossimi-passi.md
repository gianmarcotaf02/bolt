# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

Ultimo aggiornamento: **2026-10-16**, dopo lo **spezzamento di `main.cpp`** (da
11.394 a **8.632** righe: il confine in `src/app_state.h`, i supporti in
`src/app_ui.*`, e fuori quattro moduli — `view_sidebar`, `view_titlebar`,
`tabs` e la **lista con le colonne**, `src/view_list.*`), il **tema chiaro**
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
Registro: **cinquantatre'** difetti trovati e risolti e **tre aperti** — la caduta
rara all'avvio (§7.54), l'ispettore che mostra una voce nascosta dal filtro (§7.55)
e il backend Direct3D spento apposta (§7.52) —, **due** cose da fare, i limiti
dichiarati sparsi dove vivono.

---

## 1. Dove siamo

| Milestone | Stato |
|---|---|
| M1 — shell e rendering | ✅ |
| M2 — icone e anteprime | 🟡 oggetti e applicazioni fatti, manca la resa monocromatica (provata e tolta su richiesta dell'autore) |
| M2b — navigazione | ✅ |
| M3 — statistiche di cartella | ✅ byte logici e **allocati**, e i numeri si aggiornano da soli quando la cartella cambia da fuori (`ReadDirectoryChangesW`, con il limite dichiarato di due livelli) |
| M5 — filtro, ordinamento, raggruppamento, selezione | ✅ |
| M5c — modalità di visualizzazione | ✅ quattro viste, celle **derivate** da una misura continua (icona, larghezza e altezza), raggruppamento in griglia e nell'elenco, ridimensionamento continuo con `Ctrl`+rotella, **selettori di vista e misura delle anteprime nella barra in fondo**, e **icone grandi chieste alla misura giusta** invece di ingrandite (`docs/02` §7.45) |
| M5d — sovrappasso e comportamento del clic | ✅ evidenzia righe e celle, e le otto chiavi di interazione: `HoverToSelect`, `UnderlineOnHover`, `ShowTooltips`, `SingleClick`, `SingleClickFoldersOnly`, `ActOnPress`, `DoubleClickToParent`, `ShowCheckboxes` |
| M5b — colonne | ✅ ridimensionabili col mouse, con filtri per valore nelle intestazioni e **filtro di testo sul nome** |
| M6 — tab, barra del titolo, palette, GoTo | 🟡 fatti tab, titolo, palette, GoTo, **barra laterale a sezioni** (fissaggi, recenti, segnalibri, volumi, cartelle standard), **pulsanti di navigazione** e **albero dei tab verificato** (creazione per trascinamento, chiusura per ramo, distacco); resta il quick access pinnato |
| M7 — operazioni su file | 🟡 copia, sposta, rinomina, cestino, clipboard interoperabile, **menu contestuale a più livelli** con **`Proprietà` in-app** e **`Proprietà di Windows`**, le **voci di primo livello della shell**, il **menu della cartella** sul fondo, i **sottomenu delle estensioni**, **`Nuovo file >`** (tipi dal registro, creazione con
contenuto), **`Invia a >`** (destinazioni della cartella SendTo, eseguite lasciandoci
cadere sopra la selezione), **drag&drop dentro e fuori** con OLE e la **navigazione
nello spazio dei nomi** col **ripristino dal cestino** e il **trascinamento sul
riquadro di navigazione** (che sul cestino è il gesto per buttare via, non una
copia). Mancano le cartelle riciclate (che si ripristinano, non si aprono) e il
trascinamento **fuori** dal cestino, che resta un limite dichiarato |
| M8 — inspector | ✅ metadati, proprietà `PROPSYS`, conteggio per tipo, **visualizzatore di testo** (righe, minimap, spazi visibili, andare a capo, ingrandimento, codifica), **anteprima immagine** decodificata con adattamento e ingrandimento e **griglia di sfondo** (`InspectorHideGrid`). Le voci di spazio dei nomi mostrano le colonne della shell (percorso originale, data di eliminazione) |
| M9 — configurazione e temi | 🟡 pannello delle opzioni (`Ctrl+,`) con i controlli tipizzati, preferenze ricordate in `%APPDATA%\Bolt\settings.ini`, **lingua su tutte le superfici** e **tavolozza chiara e scura** con `SystemColorScheme` che segue Windows; mancano la mappa tasti rimappabile, le viste per cartella e i layout salvabili |
| M10 — backend D3D11 | 🟡 `IRenderer` dietro cui stanno **OpenGL** e **Direct3D 11** (stessi shader in HLSL, stesso batcher di quadrilateri), con scelta all'avvio (`BOLT_BACKEND`) e fallback all'OpenGL. Lo shader HLSL incorporato pero' **non compila** — `point` e' una parola riservata — quindi il Direct3D non e' mai partito davvero e il fallback faceva sembrare tutto a posto. Il rimedio e' una parola e non e' applicato: accenderebbe il secondo backend **per default**, e quel percorso non ha avuto un giro completo. `docs/02` §7.52 |

L'applicazione è **usabile**: si naviga, si filtra, si raggruppa, si seleziona, si
aprono tab, si salta con GoTo, si guardano anteprime e statistiche ricorsive, e si
lavorano i file — inclusa l'apertura delle proprietà di Windows.

---

## 2. Debito tecnico dichiarato

Ordinato per quanto morde.

0. **Una caduta rara all'avvio — localizzata, aperta.** `docs/02` §7.54: **4
   cadute su 4**, coi breadcrumb, hanno l'ultima riga dentro la lettura
   dell'**accesso rapido** (`ShellLocation::Read`), prima della prima chiamata alla
   shell. Il tasso va a raffiche (`0 su 30`, poi `5 su 55`, poi `0 su 40`, `0 su 40`,
   `0 su 60`), quindi dipende dallo stato della macchina, non dal percorso di codice;
   `0 su 12` sotto AddressSanitizer, che quindi non ne da' una pila. **Per chiuderla
   serve un dump** (`LocalDumps` di WER + un debugger, oppure `devenv /debugexe`
   aperto ad aspettare): tutto il resto e' scritto — dove guardare e quanto spesso.
1. **`src/main.cpp` e' a 8.632 righe, ed erano 11.394.** Lo spezzamento e' in
   corso: il confine che mancava ora c'e' (`src/app_state.h` per lo stato e i tipi
   condivisi, `src/app_ui.*` per i supporti), e sono usciti quattro moduli — tre di
   vista (`src/view_sidebar.*`, `src/view_titlebar.*` e la **lista con le colonne**,
   `src/view_list.*`) piu' la parte pura del modello dei tab (`src/tabs.*`). La lista
   e' uscita intera, e con essa le domande su una voce — cursore, selezione, percorso,
   icona — che prima l'input e il disegno si ricalcolavano ognuno per conto suo.
   Il numero e' **risalito** di 156 righe con il tema (le due opzioni, le tre voci di
   palette, il caricamento e il salvataggio): un pezzo nuovo scritto dove c'e' ancora
   il collo di bottiglia. Restano dentro **l'inspector, la barra di stato, i menu, i
   comandi e l'input**: sono circa duemilacinquecento righe, e adesso si spostano una
   per volta **senza toccare quello che resta**, perche' la direzione delle
   dipendenze e' fissata. I confini dei componenti ci sono (`GroupMenu`,
   `ContextMenu`, `file_ops`, `dir_stats`, `shell_*`, `drag_drop`) ma quelli della
   vista vivono ancora nello stesso file. Spostarli e' un lavoro meccanico ma largo,
   e va fatto quando c'e' spazio per riverificare tutto.
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

---

## 3. Prossimi passi candidati

**Tre numeri, che sono tre cose diverse e non vanno confusi.**

- **Cinquantatre'** difetti **trovati e risolti**: il registro è `docs/02` §7, uno per
  uno, con sintomo, causa e correzione. È un cumulativo, non una lista di lavoro.
- **Due** cose **da fare**: la lista qui sotto.
- I **limiti dichiarati**: cose che si è scelto di non fare, scritte dove vivono con
  la ragione accanto (il tetto di 1 MB nella lettura dei testi, i modelli `ShellNew`
  con valore `Command`, le 23 colonne della specifica contro le 6 fatte, il ritaglio
  per-quad in `AGENTS.md` §8, le sei opzioni che il pannello mostra spente). Ogni
  volta che nei documenti c'è scritto "dichiarato", vuol dire questo: non risolto, e
  detto.

Qualche voce dei cinquanta porta con sé un residuo dichiarato — §7.34 ha risolto
l'oggetto dati ma la voce riciclata che non viene presa resta un limite — e il
conteggio non lo nasconde: il residuo è scritto nella voce stessa.

In ordine di valore, non di facilità.

> **M9 — il pannello delle opzioni** (`Ctrl+,`), **l'allineamento delle scorciatoie**
> (`Ctrl+P`/`F4` al GoTo, `Ctrl+Shift+P` ai comandi, `Ctrl+,` alle opzioni), **finire
> la lingua**, **salire da una radice di volume**, **le tre cose viste nell'originale**
> (selettori di vista e anteprime in fondo, filtro del nome, `RECENTS`),
> **l'albero dei tab**, **`InspectorHideGrid`**, il **backend Direct3D 11** e il
> **tema chiaro** erano voci
> di questa lista, e sono fatte: vedi `docs/04` M9, M6, M8 e M10, e §7.40/§7.41/§7.42
> di `docs/02`. Restano della M9 la **mappa tasti rimappabile** sui 319 comandi, le
> **viste per cartella** e i **layout salvabili**; di M4 il **punteggio** dei recenti,
> che adesso e' solo l'ordine
> di visita.

### L'ordine deciso il 2026-10-16

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
3. **Il dialogo di progressione** di M7: oggi una copia da 5 GB non dice a che punto
   e' e non si ferma. E' il prossimo, e si tocca con le mani. Primo passo, scritto
   qui perche' non vada perso: `file_ops.cpp` esegue `IFileOperation` in modo
   sincrono; per avere il conto serve un `IFileOperationProgressSink` (quattro
   metodi contano: `PreCopyItem`, `PostCopyItem`, `UpdateProgress`, `PostDeleteItem`)
   agganciato con `Advise`, `FOF_SILENT` per spegnere la finestra della shell (che
   l'originale sostituisce con la sua `OperationStatusWindow`), l'operazione su un
   **thread di lavoro** e lo stato (byte fatti, byte totali, voce in corso, annulla)
   in `AppState`; il pannello e' un modale come quello delle proprieta', e il
   pulsante chiama `IFileOperation::Abort`. Si verifica con un file grande nella
   sandbox: il conto deve muoversi e l'annullamento deve fermare davvero.
4. Il motivo dell'ordine: lo spezzamento non si vede, quindi farlo tutto di fila sono
   tre sessioni senza niente da guardare; e cominciarlo **adesso** costa meno di
   cominciarlo dopo, perche' ogni funzione nuova che arriva prima paga il file unico.
3. **B e C si possono invertire** se si preferisce vedere subito qualcosa di grosso:
   il tema chiaro si tocca con gli occhi, il dialogo di progressione con le mani.

Le due cose da fare, che restano tali:

1. **Il trascinamento fuori dal cestino** — l'oggetto dati adesso c'è e le voci
   normali escono davvero, ma una voce **riciclata** non viene presa (la
   destinazione dichiara "copia" e non copia). La causa non è ricostruita: §7.34 di
   `docs/02`. Se non si trova, resta un limite dichiarato.
2. **Spezzare `main.cpp`** — **8.632 righe** (erano 11.394) e ventiquattro moduli: il confine c'è, e
   file che lo contiene resta il collo di bottiglia dichiarato fin dall'inizio, ma non piu' intatto:
   vedi la voce 1.


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
giro del secondo backend dopo §7.52).

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
che si hanno.

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
