# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

Ultimo aggiornamento: dopo i verbi della shell nel menu contestuale.

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
| M6 — tab, barra del titolo, palette, GoTo | 🟡 fatti tab, titolo, palette, GoTo; mancano quick access pinnato, drag&drop e albero dei tab |
| M7 — operazioni su file | 🟡 copia, sposta, rinomina, cestino, clipboard interoperabile, **menu contestuale** con Proprietà di Windows e le **voci di primo livello della shell**. Mancano drag&drop, i sottomenu della shell e il ripristino dal cestino |
| M8 — inspector completo | ⬜ |
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

1. **I sottomenu del menu contestuale** — `Invia a`, `7-Zip`, `WinRAR`,
   "Dare accesso a", "Includi nella raccolta": sono quattro o cinque voci su
   venti quelle che restano fuori. Le voci di primo livello della shell ci sono
   già (`IContextMenu` → `QueryContextMenu` → `InvokeCommand`), quindi è il
   completamento naturale di ciò che c'è: serve che il componente sappia
   annidare — e che i sottomenu si facciano popolare da
   `IContextMenu2::HandleMenuMsg(WM_INITMENUPOPUP)`, che è la parte davvero
   complicata.
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
