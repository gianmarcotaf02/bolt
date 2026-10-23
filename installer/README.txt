Bolt - file manager per Windows (clone didattico di File Pilot)
=====================================================================

Cosa hai installato
-------------------
Bolt.exe piu' i due shader che disegnano l'interfaccia. Nient'altro: nessun
runtime da installare (il CRT e' statico, le uniche dipendenze sono DLL di
Windows) e nessun servizio.

Per toglierlo: App e funzionalita' -> Bolt -> Disinstalla. Toglie il programma,
il collegamento e la voce di registro; le tue preferenze restano, e te lo chiede
prima di eventualmente cancellarle.

Dove sta la configurazione
--------------------------
Un solo file: %APPDATA%\Bolt\settings.ini, con le preferenze (lingua, tema,
opzioni). Nessun elenco di recenti, nessuna cronologia, nessuna cache su disco:
recenti, segnalibri e anteprime vivono in memoria e muoiono con il processo.
Il percorso lo dice il programma stesso (palette -> "Where the settings file
is"). Dettagli in docs/06-privacy.md del repository.

Requisiti
---------
Windows 10 o 11 a 64 bit. Direct3D 11 (il predefinito) o OpenGL 3.3 come
ripiego: BOLT_BACKEND=opengl forza il secondo.

Se qualcosa non parte
---------------------
Accanto all'eseguibile compare "startup-error.log" solo quando c'e' un errore,
con il messaggio. Con BOLT_DIAG=1 il programma scrive "bolt-diag.log" con cosa
ha fatto: quale backend disegna, quale cartella di shader ha aperto, i verbi
della shell, le operazioni sui file.

Versione portatile
------------------
Lo stesso programma c'e' anche come zip: si scompatta dove si vuole e si lancia
Bolt.exe, senza installare niente. Serve cosi' com'e', perche' la cartella
"shaders" accanto all'eseguibile e' tutto quello che cerca.
