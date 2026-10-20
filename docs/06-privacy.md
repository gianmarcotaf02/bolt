# Privacy e dati

Regola di progetto, decisa dall'autore:

> **Nessun file personale. Nessuna impostazione salvata. Niente su disco.**

Bolt non deve lasciare traccia di cosa l'utente ha aperto, guardato, cercato o
configurato. Non è una limitazione tecnica da superare in una milestone futura: è
un **requisito**, e vale come tale.

---

## 1. Perché

Uno strumento che mostra l'intero contenuto di un disco vede più di qualunque
altra applicazione: nomi di file, percorsi, struttura delle cartelle, quali
documenti esistono e quando sono stati modificati.

Scrivere quella conoscenza su disco — anche solo come «elenco dei recenti» o
«ultima cartella aperta» — trasforma un programma che *mostra* dati in un
programma che ne **conserva una copia**, in un posto che l'utente non controlla e
di cui non sa niente. Il file manager diventa esso stesso la cosa da cui difendersi.

## 2. Cosa significa, concretamente

**Vietato:**

- Scrivere in `%APPDATA%`, `%LOCALAPPDATA%`, `%PROGRAMDATA%` o accanto
  all'eseguibile, qualunque cosa che descriva l'attività dell'utente. **Una sola
  eccezione**, chiesta dall'autore e descritta in §4-bis: le preferenze, in un file
  di testo che dice cosa contiene, che non nomina nessun file e nessuna cartella
  visitata.
- Registro di Windows: nessuna chiave, di nessun tipo.
- Elenchi di recenti, cronologia di navigazione, segnalibri o ultima-cartella
  **persistenti**. Restano in memoria e muoiono con il processo.
- Cache su disco di anteprime, miniature, statistiche di cartella o indici.
- Qualunque forma di telemetria, anche anonima, anche locale.
- Impostazioni di interfaccia, temi, layout o mappe tasti salvati di nascosto.

**Ammesso:**

- Scrivere su disco quando è **l'utente a chiederlo** con un'azione esplicita — è
  il caso delle operazioni su file, che sono il mestiere del programma.
- Un registro di errori, **solo se si verifica un errore**, contenente il messaggio
  d'errore e nient'altro. Oggi è `startup-error.log` accanto all'eseguibile, e non
  viene creato quando non ci sono errori.
- Lettura: le impostazioni dell'originale, se mai servissero, si **leggono** e non
  si scrivono.

## 3. Stato attuale — verificato

Al momento della stesura:

| verifica | esito |
|---|---|
| Scritture di file nel codice | `startup-error.log` (solo su errore) e il setup scaricato dall'aggiornamento (§3-bis) |
| Riferimenti a `%APPDATA%` / `%LOCALAPPDATA%` | **uno**: `\Bolt\settings.ini`, le preferenze (§4-bis) |
| Accessi al registro | **nessuno** |
| Cache su disco | **nessuna** |
| Richieste di rete | **una sola**, e solo dal comando esplicito `Controlla aggiornamenti` (§3-bis) |

L'unico `CreateFileW` che non è una lettura, oltre al setup dell'aggiornamento, è il registro di errori. La cache
delle anteprime, delle icone e delle statistiche vive **solo in memoria** ed è
legata alla sessione: è già così, e va mantenuto così.

I recenti e i segnalibri esistono, sono usati e **non vengono scritti**: la tabella
dello spec li chiama `StoreRecents: Session`, e quella modalità non è una casella
mancante — è la scelta.

`StoreRecents` ha però tre valori nel riferimento, e da M4 ci sono tutti e tre
(`src/recents.*`): `Session`, `Persistent`, `LocalDisk`. Nessuno dei tre scrive su
disco — il modo dice **cosa si registra** (tutte le cartelle, o solo quelle dei
volumi locali fissi), non dove finisce. `Persistent` nel riferimento sopravvive
alla sessione nel blocco `"Recents": [` del file di sessione; qui resta in memoria,
e il limite e' dichiarato in `docs/04` M4. La scelta del modo **non si salva**: sta
nel pannello delle opzioni (`Ctrl+,`), la riga dice «vale per questa sessione», e il
file `settings.ini` non ne contiene la chiave — verificato il 2026-10-18.

## 3-bis. L'unica rete: gli aggiornamenti

Il programma manda una richiesta di rete in **un solo caso**, e solo quando l'utente
lo chiede: il comando `Controlla aggiornamenti` nella palette (`src/update.*`). Non c'è
un timer, non c'è un controllo all'avvio, e il comando **non ha un tasto di partenza**
— non si può premere per sbaglio. Una richiesta a GitHub è telemetria di fatto: se
partisse da sola, questo documento non sarebbe vero.

Cosa parte nella richiesta: l'indirizzo `api.github.com/repos/gianmarcotaf02/bolt/releases/latest`,
l'intestazione `User-Agent: Bolt/<versione>` (obbligatoria per l'API di GitHub), e
nient'altro. Nessun identificatore, nessun percorso, nessun elenco di quello che si è
guardato, nessuna statistica.

Cosa si scrive su disco: il setup scaricato, in `%TEMP%\Bolt-update\`. Se lo
**SHA-256** non corrisponde a quello pubblicato nella release, il file viene
**cancellato** e non si installa; se corrisponde, parte il setup in silenzioso, che
legge quel file e lo lascia lì (la cartella temporanea di Windows non è nostra, e
ripulirla mentre il setup la usa non si può). Un controllo successivo sovrascrive il
file con lo stesso nome.

`BOLT_UPDATE_API` è un **attrezzo da banco**, non una funzione del prodotto: sposta
l'indirizzo del foglio delle release su un server locale per provare download, impronta
e rifiuto senza pubblicare una release vera. Il setup passa comunque dal confronto
dell'impronta, quindi non aggira il controllo.

---

## 4. La conseguenza scomoda

Questa regola **contraddice** il piano originale della milestone 9, che prevedeva i
tre file JSON sotto `%APPDATA%\Voidstar\FilePilot` (`FPilot-Config.json`,
`FPilot-UserData.json`, `FPilot-Session.json`). Quelli sono il comportamento del
programma originale, osservato e documentato in `docs/03`.

Copiare anche quella parte significherebbe copiare l'unica cosa che **non** va
copiata. La milestone è stata riscritta di conseguenza: mappa tasti, temi, viste e
layout restano, ma in memoria.

Il costo è reale e va dichiarato: a ogni avvio si riparte dalle impostazioni
predefinite. Se un giorno la persistenza servirà davvero, dovrà essere **esplicita
e scelta dall'utente** — un comando che dice «salva la configurazione in questo
file», con scritto cosa ci finisce dentro — e non una cartella creata di nascosto
al primo avvio.

## 4-bis. Cosa si scrive, oggi

C'e' **un** file, e c'e' perche' l'autore l'ha chiesto («la lingua deve impostarsi in
maniera fissa»):

    %APPDATA%\Bolt\settings.ini

Testo semplice, una riga per preferenza (`language=italian`, `showTooltips=1`, …),
scritto a ogni cambio e riletto a ogni avvio. Contiene **solo come si vuole che il
programma si comporti**: nessun nome di file, nessuna cartella visitata, nessun
elenco di cosa si e' guardato. Si cancella a mano, e cancellarlo riporta tutto ai
valori di partenza.

**L'unica cosa che sa di cartelle** e' la *pagina di avvio*, se la scegli tu: una riga
`startupFolder=` con la cartella che hai chiesto di aprire all'avvio. Non e' una traccia
di dove sei stato — e' una preferenza, come il tema, e il programma non la cambia da
solo: la si imposta con due comandi (`Avvio: la cartella aperta`, `Avvio: Questo PC`) o
dalla riga `Avvio` fra le opzioni, e senza quella riga il file non contiene nessun
percorso. **"Riapri l'ultima cartella" non c'e' e non ci sara'**: quella sarebbe
cronologia, per quanto comoda. Il percorso lo dice l'applicazione, con un comando della palette
(`Dove sta il file delle impostazioni`): non e' una cartella creata di nascosto.

## 5. Cosa fare se il requisito sembra in conflitto con una richiesta

Chiedere. Non introdurre una scrittura su disco «piccola», «innocua» o
«temporanea» per far funzionare qualcosa: è esattamente il modo in cui questo tipo
di requisito si perde, un compromesso ragionevole alla volta.

---

## 6. Due regole diverse, che è facile confondere

Questo documento parla di **ciò che scrive l'applicazione**. La regola su **ciò che
tocca chi ci lavora** sta in `AGENTS.md` §0, ed è più stringente:

| | riguarda | regola |
|---|---|---|
| `AGENTS.md` §0 | l'agente, la macchina che lavora al progetto | **non tocca file personali, non ne salva copia** |
| questo documento | il programma Bolt, per chi lo usa | **non scrive impostazioni né tracce su disco** |

Sono due cose distinte e servono entrambe. La prima protegge chi *costruisce* il
programma; la seconda protegge chi lo *usa*.

Nota pratica per chi verifica: le prove si fanno in `analysis/tmp/`, con file
finti, **mai** nelle cartelle reali di chi lavora — nemmeno solo per una cattura,
perché uno screenshot di una cartella vera ne contiene i nomi dei file.
