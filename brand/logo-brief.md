# Bolt — identità visiva e logo

Brief per la generazione del logo. La parte in inglese è **pronta da incollare**
in un generatore di immagini: i generatori rendono meglio con istruzioni in
inglese e con una descrizione visiva concreta. Il resto è la direzione artistica
che rende il risultato *professionale* invece che casuale — un prompt senza
vincoli produce un fulmine generico, e un fulmine generico lo ha già chiunque.

---

## 1. Perché "Bolt"

Il nome porta due significati e il logo deve tenerli entrambi:

- **il fulmine** — velocità, risposta istantanea. È la promessa del prodotto: un
  file manager che si apre e naviga senza attesa.
- **il bullone** — precisione meccanica, fissare insieme parti. È il carattere
  del prodotto: nativo, compatto, costruito, non decorato.

L'errore da evitare è il fulmine urlato: giallo, spigoloso, da supereroe o da
app di consegne. Bolt è **sobrio e preciso**. La velocità si comunica con la
geometria e il contrasto, non con il colore acceso e le scintille.

## 2. Il simbolo

La direzione principale è **un simbolo solo, non un marchio figurativo completo**:
una **cartella la cui piega si risolve in un fulmine**.

La cartella dice "file". Il fulmine dice "istantaneo". Sono lo stesso segno, non
due segni accostati: la squadretta della cartella non è sovrapposta al fulmine,
**è** il fulmine. Se il fulmine si può cancellare senza rompere la cartella, il
logo non è finito.

Costruzione e vincoli:

- **Griglia**: costruito su una griglia quadrata, spessori uniformi, angoli
  arrotondati coerenti con l'interfaccia (l'app disegna ogni forma a
  distanza firmata, quindi le curve sono continue — il logo deve avere la stessa
  qualità di curva).
- **Un colore pieno**, nessuna sfumatura, nessun riflesso, nessuna ombra
  esterna. L'app non usa gradienti: un logo con gradienti sembrerebbe di un altro
  prodotto.
- **Deve leggersi a 16 px.** Il test è questo, non il rendering grande: se a
  16 px il fulmine si chiude in una macchia, va semplificato fino a funzionare.
- **Deve funzionare su fondo scuro e su fondo chiaro**, e in **monocromia**.
- **Negativo**: preferibile al positivo, così funziona su qualsiasi fondo.

Alternative, se la principale non convince:

1. **Due righe sfalsate** che formano un fulmine — richiama la lista di file.
2. **Un bullone visto in sezione** con un fulmine nel foro — più letterale, più
   freddo, funziona come marchio secondario.
3. **Solo il fulmine**, costruito però con la stessa geometria della cartella —
   il ripiego più sicuro, e il meno caratterizzante.

## 3. Palette — i colori reali del prodotto

Non sono scelti per il logo: sono **i colori dell'applicazione**. Un logo con
colori diversi da quelli dell'interfaccia è la prima cosa che fa sembrare un
prodotto non finito.

| ruolo | valore | uso nel logo |
|---|---|---|
| accento | `#4C8DFF` | il fulmine, o il logo intero |
| accento scuro | `#2F4C7E` | variante per fondi chiari, stato stampato |
| testo chiaro | `#E6E7EA` | positivo su fondo scuro |
| testo attenuato | `#9195A0` | dettagli secondari |
| fondo finestra | `#1B1C1F` | sfondo dell'icona in tema scuro |
| pannello | `#232428` | secondo piano |

Il marchio è **azzurro su neutro scuro**. Non blu aziendale, non viola, non
arcobaleno: un solo accento, come nell'interfaccia.

## 4. Prompt pronto — simbolo principale

> Professional software logo for "Bolt", a fast native Windows file manager.
> A single square app icon containing one flat vector mark: a minimal folder
> silhouette whose folded corner resolves into a lightning bolt — the folder and
> the bolt are the same shape, not two shapes combined.
>
> Style: flat vector, solid fill, no gradients, no reflections, no outer shadow,
> no glow, no text, no letters. Uniform stroke weight, geometrically constructed
> on a square grid, softly rounded corners with continuous curves. Confident and
> restrained: precision instrument, not a superhero.
>
> Color: the mark in solid azure #4C8DFF on a deep neutral charcoal #1B1C1F
> square background with subtly rounded corners. Single accent color only.
>
> Composition: centered, generous margin around the mark, optically balanced,
> reads clearly at 16x16 pixels.
>
> Output: 1024x1024, crisp edges, transparent-safe, suitable for a Windows .ico.

## 5. Prompt pronto — varianti

**In negativo (per fondi chiari):**

> Same mark as above, but drawn as negative space: a solid azure #4C8DFF rounded
> square with the folder-bolt shape cut out in #E6E7EA. Flat vector, no
> gradients, single accent color, reads at 16x16.

**Monocromia (per la barra delle applicazioni e i contesti a un colore):**

> Same folder-bolt mark in a single flat color #E6E7EA on transparent
> background. No background shape, no color variation, pure silhouette. Must
> remain recognizable at 16x16 pixels.

**Lockup orizzontale (per il sito e le intestazioni):**

> Horizontal lockup: the folder-bolt mark on the left, the wordmark "Bolt" to its
> right in a clean geometric sans-serif, medium weight, tight letter spacing,
> color #E6E7EA, on #1B1C1F. The mark's stroke weight matches the wordmark's
> stroke weight. Flat vector, no gradients, decisive and technical.

## 6. Cosa non deve essere

- Fulmine giallo, arancione o multicolore.
- Fulmine da supereroe, da app di consegne, da centrale elettrica o da
  emergenza.
- Cartella e fulmine sovrapposti come due oggetti distinti.
- Gradienti, smussi, ombre portate, riflessi, effluvi, scintille.
- Prospettiva o pseudo-3D.
- Testo dentro l'icona quadrata.
- Dettagli che spariscono sotto i 32 px.

## 7. Consegne

Windows non usa una sola immagine: usa la stessa icona a molte dimensioni, e le
versioni piccole vanno **ridisegnate a mano**, non rimpicciolite.

| file | dimensioni | note |
|---|---|---|
| `bolt.ico` | 16, 20, 24, 32, 40, 48, 64, 256 | 16 e 20 semplificate a mano |
| `bolt-symbol.svg` | vettoriale | simbolo, per il repository |
| `bolt-lockup.svg` | vettoriale | simbolo + nome, per README e sito |
| `bolt-mono.svg` | vettoriale | monocromia |
| `bolt-256.png` | 1024 o 256 | per il README su GitHub |

Le versioni a 16 e 20 px sono quelle che contano davvero: sono l'unica che
l'utente vede nella barra delle applicazioni e in Esplora risorse. Vanno
verificate **a dimensione reale**, non ingrandite.

---

## Nota per chi userà questo documento

Il logo è l'ultima cosa da fare, non la prima. Un prodotto che funziona con un
logo mediocre è credibile; un logo eccellente su un prodotto che non copia i file
non lo è. Questa sezione esiste perché il nome è deciso e i colori sono
verificati — non perché sia il momento di disegnarlo.

---

## Stato — icona in opera

L'icona è agganciata (commit sulla cartella col fulmine, blu `#3082FC`).

**Provenienza.** Il file fornito era un `.ico` con **una sola immagine, 32×27 e
non quadrata**. Windows ne vuole molte, quadrate: ricomposta su tela 32×32 e
rigenerata a 16/20/24/32/48/64/256 in `brand/bolt.ico`, incorporata
nell'eseguibile dalla risorsa `IDI_BOLT` (`src/bolt.rc`). Verificata nella barra
delle applicazioni reale.

**Limite dichiarato.** Le misure **16, 20, 24 e 32 sono ribassate dal master a 32
px** e sono buone: è lì che l'icona conta, ed è lì che si vede. Le misure **48, 64
e 256 sono ingrandite** dallo stesso master e sono quindi morbide: si nota nella
vista "icone extra grandi" di Esplora risorse.

Per averle nitide serve un **sorgente ad alta risoluzione** (512 o 1024 px, o un
vettoriale). Rigenerare è meccanico: si rilancia la stessa procedura sul nuovo
sorgente e le misure piccole restano identiche.
