# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

Ultimo aggiornamento: dopo il nucleo di M7 (operazioni su file).

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
| M6 — tab, barra del titolo, palette, GoTo | 🟡 fatti tab, titolo, palette, GoTo; mancano quick access pinnato, drag&drop e albero dei tab |
| M7 — operazioni su file | 🟡 nucleo fatto: copia, sposta, rinomina, cestino, clipboard interoperabile; mancano ripristino, drag&drop, menu contestuale |
| M8 — inspector completo | ⬜ |
| M9 — configurazione e temi | ⬜ |
| M10 — backend D3D11 | ⬜ |

L'applicazione è **usabile**: si naviga, si filtra, si raggruppa, si seleziona, si
aprono tab, si salta con GoTo, si guardano anteprime e statistiche ricorsive.

---

## 2. Debito tecnico dichiarato

Ordinato per quanto morde.

1. **`src/main.cpp` è a ~3.860 righe.** Il confine del componente a gruppi
   (`GroupMenu`) esiste ed è pulito, ma vive nello stesso file. Spezzarlo richiede
   che `AppState` e i tipi condivisi escano dall'anonimo namespace in un header:
   è un lavoro meccanico ma largo, e va fatto quando c'è spazio per riverificare
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

1. **Il resto di M7** — ripristino dal cestino, drag&drop in ingresso e in
   uscita, menu contestuale della shell. Il menu contestuale porta con sé il
   **terzo contesto** del componente a gruppi, che è il momento giusto per
   spostarlo in un file suo.
2. **I tre difetti dichiarati di M7** (vedi `docs/04-roadmap.md`): la selezione
   che cade sul file sbagliato dopo un incolla con conflitto — quello con il
   `Canc` subito dopo è pericoloso — le statistiche non invalidate, e il
   ripristino dal cestino.
3. **M8 — inspector completo** — proprietà estese (`PROPSYS`), anteprima immagine
   con zoom, visualizzatore di testo con minimap e numeri di riga.
4. **M9 — configurazione e temi** — i tre file JSON, la mappa tasti sui 319
   comandi, i 35 token di tema. È anche dove i **recenti e i segnalibri** smettono
   di essere di sola sessione (`StoreRecents: Persistent`).
5. **Raggruppamento in griglia** e **ridimensionamento continuo** della cella.
6. **Spezzare `main.cpp`** — ormai sono ~4.200 righe e quattro moduli esterni
   (`file_ops`, `dir_stats`, `shell_icons`, `shell_preview`): il confine c'è, ma
   il file che lo contiene è diventato il collo di bottiglia.

---

## 4. Come si costruisce e si verifica

Non c'è `cmake` nel PATH della shell. Il percorso è:

```powershell
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build build --config RelWithDebInfo
```

Il ciclo visivo è: costruire, lanciare su una cartella nota, catturare, guardare.

```powershell
./build/RelWithDebInfo/Bolt.exe "C:\Users\Gianmarco\Downloads"
powershell -ExecutionPolicy Bypass -File tools/capture-window.ps1 `
    -ProcessName Bolt -OutputPath analysis/tmp/shot.png
```

Le **shader** si leggono dalla sorgente (`REA_SHADER_DIR`), quindi una modifica a
`src/shaders/*` non richiede ricompilare.

`analysis/tmp/` raccoglie i riscontri: `qualita-prima-dopo.png`,
`viste-tiles.png`, `m5-*.png`, `m6-*.png`, `m3-progress.png`.

---

## 5. Trappole già pagate — non ripagarle

**Cattura schermo.** La finestra deve essere in primo piano o la cattura riprende
ciò che sta sopra. Con `SetWindowPos(h, HWND_TOPMOST, …)` e poi
`HWND_NOTOPMOST` funziona sempre; va tolto il topmost alla fine.

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
davvero: la prova è stata fatta su `analysis/tmp/m7-sandbox`, mai su file
personali, e la sandbox va ricreata prima di ogni giro. Cancellare
`alfa - Copia.txt` per sbaglio è il modo in cui un test diventa un danno.

**`tools/send-keys.ps1`** esiste perché l'escaping di PowerShell dentro bash
mangiava i caratteri e i tasti non arrivavano: uno script su file è l'unico modo
affidabile di ripetere una prova di input.

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
