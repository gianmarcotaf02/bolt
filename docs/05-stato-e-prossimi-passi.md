# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

Ultimo aggiornamento: dopo dragUltimo aggiornamento: dopo "Nuovo file >" e "Invia a >", i due sottomenu che la shell non sa eseguire.drop, "Nuovo file >" e "Invia a >".

---

## 1. Dove siamo

| Milestone | Stato |
|---|---|
| M1 — shell e rendering | ✅ |
| M2 — icone e anteprime | 🟡 mancano solo le icone monocromatiche per tema scuro |
| M2b — navigazione | ✅ |
| M3 — statistiche di cartella | 🟡 mancano byte allocati e invalidazione da `ReadDirectoryChangesW` |
| M5 — filtro, ordinamento, raggruppamento, selezione | ✅ |
| M5c — modalità di visualizzazione | 🟡 manca il raggruppamento in griglia |
| M5d — sovrappasso del mouse | 🟡 evidenzia righe e celle; mancano `HoverToSelect`, `UnderlineOnHover`, `ShowTooltips` |
| M6 — tab, barra del titolo, palette, GoTo | 🟡 fatti tab, titolo, palette, GoTo, **barra laterale a sezioni** (fissaggi, segnalibri, volumi, cartelle standard) e **pulsanti di navigazione**; mancano drag&drop e albero dei tab |
| M7 — operazioni su file | 🟡 copia, sposta, rinomina, cestino, clipboard interoperabile, **menu contestuale a più livelli** con Proprietà di Windows, le **voci di primo livello della shell**, il **menu della cartella** sul fondo, i **sottomenu delle estensioni**, **`Nuovo file >`** (tipi dal registro, creazione con
contenuto), **`Invia a >`** (destinazioni della cartella SendTo, eseguite lasciandoci
cadere sopra la selezione), **drag&drop dentro e fuori** con OLE e la **navigazione
nello spazio dei nomi** col **ripristino dal cestino**. Mancano il trascinamento sul
riquadro di navigazione e le cartelle riciclate (che si ripristinano, non si aprono) |
| M8 — inspector | ✅ metadati (creazione, modifica, accesso, percorso), proprietà `PROPSYS` (dimensioni immagine, file system e spazio dei volumi), conteggio per tipo, e il **visualizzatore di testo** (righe, minimap, spazi visibili, andare a capo, ingrandimento, codifica e terminatori). Due voci dichiarate non fatte: zoom dell'anteprima immagine e le due etichette del cestino |
| M9 — configurazione e temi | ⬜ **vincolata**: niente file di impostazioni, vedi `docs/06` |
| M10 — backend D3D11 | ⬜ |

L'applicazione è **usabile**: si naviga, si filtra, si raggruppa, si seleziona, si
aprono tab, si salta con GoTo, si guardano anteprime e statistiche ricorsive, e si
lavorano i file — inclusa l'apertura delle proprietà di Windows.

---

## 2. Debito tecnico dichiarato

Ordinato per quanto morde.

1. **`src/main.cpp` è a ~5.300 righe.** I confini dei componenti ci sono
   (`GroupMenu`, `ContextMenu`, `file_ops`, `dir_stats`) ma vivono tutti nello
   stesso file. Spezzarlo richiede che `AppState` e i tipi condivisi escano
   dall'anonimo namespace in un header: è un lavoro meccanico ma largo, e va fatto
   quando c'è spazio per riverificare
   tutto.
2. **Nessun controllo di versione locale.** Il repository è su GitHub
   (`gianmarcotaf02/bolt`) e il sistema salva con commit automatici a ogni turno,
   ma non c'è una convenzione di branch: si lavora su `main`.
3. **Lo zoom ricampiona il testo.** A `scale` diverso da 1.0 sia il corpo che il
   secondo corpo vengono scalati, quindi il ricampionamento torna. Eliminarlo
   richiede una rasterizzazione a runtime per ogni dimensione: è un altro
   mestiere, non un difetto da correggere adesso.

---

## 3. Prossimi passi candidati

In ordine di valore, non di facilità.

1. **M9 — configurazione e temi**, che è la milestone successiva, ma va ripensata:
   `docs/06` vieta di scrivere impostazioni su disco, quindi i tre file JSON
   dell'originale non si copiano. Restano i temi e le mappe tasti, tenute **in
   memoria** e dichiarate per quello che sono.
2. **Il riquadro di navigazione come destinazione di caduta** — si può lasciare
   cadere su una riga di cartella e sul fondo della lista, non sui collegamenti
   della barra laterale: il meccanismo c'è (`docs/02` §7.22), manca il bersaglio.
   Con quello, trascinare qualcosa **sul cestino** diventa il gesto per buttarlo via.
3. **Colonne ridimensionabili e filtri per colonna** — nel riferimento ci sono
   entrambi: le intestazioni hanno un'icona di filtro e i bordi si trascinano. Qui
   le colonne sono frazioni fisse della larghezza.
4. **Trascinare fuori dalle cartelle di spazio dei nomi** — oggi è rifiutato con un
   messaggio, e la ragione è scritta (`docs/02` §7.25): l'oggetto dati si costruisce
   dai nomi. Con i PIDL che ormai si hanno in mano si può fare, ed è il passo che
   chiude il cerchio.
2. **Il costo dell'interrogazione della shell** — la prima apertura dopo l'avvio
   costa ~350 ms e le estensioni moderne di Windows 11 compaiono solo dalla
   seconda; in regime sono ~65 ms e **22 handle di kernel** per apertura, dentro
   le estensioni. Da decidere: chiedere la shell su richiesta, invece che a ogni
   tasto destro. Vedi `docs/04-roadmap.md`, sezione M7.
3. **Drag&drop in ingresso e in uscita** con descrizione. In ingresso è
   `RegisterDragDrop` + `IDropTarget`; in uscita `IDataObject`. **Attenzione:**
   un trascinamento OLE non si riesce a simulare come si simula un clic, quindi
   va progettato in modo da poter essere verificato a pezzi. Per la metà del
   lavoro serve qualcosa come `tools/send-mouse.ps1`, che ora esiste.
4. **Il browsing dello spazio dei nomi** — è il prerequisito vero del ripristino
   dal cestino: la nostra navigazione è basata sui percorsi, il cestino è fatto di
   PIDL. Senza questo, il ripristino sarebbe un imballaggio sopra una funzione che
   manca. Vedi `docs/04-roadmap.md`, sezione M7.
5. **M8 — inspector completo** — proprietà estese (`PROPSYS`), anteprima immagine
   con zoom, visualizzatore di testo con minimap e numeri di riga.
6. **M9 — configurazione e temi** — **vincolata da `docs/06`**: niente file di
   impostazioni, tutto in memoria. Restano la mappa tasti sui 319 comandi e i 35
   token di tema.
7. **Raggruppamento in griglia** e **ridimensionamento continuo** della cella.
8. **Il resto di M5d** — `HoverToSelect`, `UnderlineOnHover` e `ShowTooltips`,
   più le altre chiavi di interazione dello spec. Vedi `docs/04-roadmap.md`.
9. **Spezzare `main.cpp`** — ~5.300 righe e otto moduli: il confine c'è, ma il
   file che lo contiene è diventato il collo di bottiglia.

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

`analysis/tmp/` raccoglie i riscontri: `qualita-prima-dopo.png`,
`viste-tiles.png`, `m5-*.png`, `m6-*.png`, `m3-progress.png`, `menu-*.png` (i
verbi della shell: menu aperto, fondo del menu, rotella, finestra ridotta
a 460 px).

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
**cliente**, così la stessa prova si ripete anche se la finestra si sposta.

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
