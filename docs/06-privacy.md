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
  all'eseguibile, qualunque cosa che descriva l'attività dell'utente.
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
| Scritture di file nel codice | una sola: `startup-error.log`, solo su errore |
| Riferimenti a `%APPDATA%` / `%LOCALAPPDATA%` | **nessuno** |
| Accessi al registro | **nessuno** |
| Cache su disco | **nessuna** |

L'unico `CreateFileW` che non è una lettura è il registro di errori. La cache
delle anteprime, delle icone e delle statistiche vive **solo in memoria** ed è
legata alla sessione: è già così, e va mantenuto così.

I recenti e i segnalibri esistono, sono usati e **non vengono scritti**: la tabella
dello spec li chiama `StoreRecents: Session`, e quella modalità non è una casella
mancante — è la scelta.

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

## 5. Cosa fare se il requisito sembra in conflitto con una richiesta

Chiedere. Non introdurre una scrittura su disco «piccola», «innocua» o
«temporanea» per far funzionare qualcosa: è esattamente il modo in cui questo tipo
di requisito si perde, un compromesso ragionevole alla volta.
