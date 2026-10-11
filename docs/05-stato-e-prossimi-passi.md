# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

Ultimo aggiornamento: dopo M5c (modalità di visualizzazione).

---

## 1. Dove siamo

| Milestone | Stato |
|---|---|
| M1 — shell e rendering | ✅ |
| M2 — icone e anteprime | 🟡 mancano solo le icone monocromatiche per tema scuro |
| M2b — navigazione | ✅ |
| M3 — statistiche di cartella | 🟡 mancano byte allocati e invalidazione da `ReadDirectoryChangesW` |
| M5 — filtro, ordinamento, raggruppamento, selezione | ✅ |
| M5c — modalità di visualizzazione | 🟡 mancano secondo corpo di glifi e raggruppamento in griglia |
| M6 — tab, barra del titolo, palette, GoTo | 🟡 fatti tab, titolo, palette, GoTo; mancano quick access pinnato, drag&drop e albero dei tab |
| M7 — operazioni su file | ⬜ non iniziata |
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
2. **Nessun controllo di versione.** Nessun `.git`. L'albero di lavoro è l'unica
   copia: un errore non si annulla.
3. **Il testo secondario è ricampionato.** Metà delle etichette usa scale
   frazionarie (0.8–0.95) sull'atlante dei glifi del corpo. Vedi §3, punto 1.

---

## 3. Prossimi passi candidati

In ordine di valore, non di facilità.

1. **Secondo corpo di glifi** — chiude la passata di qualità. Rasterizzare una
   seconda dimensione (13 px) su un layer nuovo, e sostituire i ~20 punti di
   chiamata che oggi usano `scale * 0.8..0.95` con `smallFont` a scala 1.0.
   Isolato, non tocca la logica. Vedi `font_atlas.h`.
2. **M7 — operazioni su file** — è il buco di prodotto più grande: copia, sposta,
   rinomina, crea, cestino reale (`IFileOperation`), clipboard `CF_HDROP`,
   drag&drop, dialogo di progressione. Porta con sé il **terzo contesto** del
   componente a gruppi (il menu contestuale), che è il momento giusto per
   spostarlo in un file suo.
3. **Raggruppamento in griglia** — richiede un impaginatore a flusso con bande a
   piena larghezza. Oggi la griglia lo disattiva e lo dichiara nella barra.
4. **M8 — inspector completo** — proprietà estese (`PROPSYS`), anteprima immagine
   con zoom, visualizzatore di testo con minimap e numeri di riga.
5. **M9 — configurazione e temi** — i tre file JSON, la mappa tasti sui 319
   comandi, i 35 token di tema. È anche dove i **recenti e i segnalibri** smettono
   di essere di sola sessione (`StoreRecents: Persistent`).

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
