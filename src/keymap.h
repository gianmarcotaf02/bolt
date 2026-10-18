// ---------------------------------------------------------------------------
// La mappa dei tasti: quale tasto fa quale comando.
//
// Prima non c'era. Le scorciatoie erano un `switch (wparam)` dentro `WindowProc`,
// quarantasei casi in cui il tasto e cosa fa stavano nella stessa riga: `case 'C': if
// (ctrl) DoCopyToClipboard(...)`. Va bene finche' nessuno le vuole cambiare — e
// appunto, cambiarle era la richiesta — perche' li' **il tasto e' il codice**, e non
// c'e' nessun posto in cui scrivere un tasto diverso.
//
// Qui il tasto e' un **dato**: una tabella `comando -> tasto`, che si legge per
// decidere, si legge per **scrivere** la scorciatoia accanto a una voce, e si puo'
// cambiare. E' la stessa mossa del tema: prima i colori erano numeri accanto al
// disegno, adesso sono un ruolo con un nome.
//
// Due cose da dire, perche' sono una scelta e non una dimenticanza:
//
//   * I tasti che **non sono comandi** restano dove sono. `Ctrl+A` che seleziona
//     tutto, `Ctrl+G` che cicla il raggruppamento, `Ctrl`+`+` che ingrandisce: sono
//     comportamenti dell'input, non voci che si possono eseguire da un menu, e
//     metterli in tabella vorrebbe dire inventare comandi che non esistono.
//   * La mappa e' **in memoria**, per la sessione: e' quel che dice `docs/04` per
//     M9. Non si scrive niente su disco senza chiederlo (`docs/06`).
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Bolt
{
// Un tasto con i suoi modificatori. `key` a zero vuol dire "nessun tasto".
struct Shortcut
{
    unsigned key   = 0;      // un codice `VK_*`
    bool     ctrl  = false;
    bool     shift = false;
    bool     alt   = false;

    bool empty() const { return key == 0; }
};

class KeyMap
{
public:
    // I tasti di partenza: quelli che l'applicazione aveva gia', scritti qui una
    // volta invece che dentro il gestore dei messaggi.
    KeyMap();

    // Il comando che |key| con quei modificatori esegue, o -1 se non ne esegue
    // nessuno. E' la domanda che fa l'input a ogni tasto premuto.
    int CommandFor(unsigned key, bool ctrl, bool shift, bool alt) const;

    // Il tasto di un comando, e come si scrive. Un comando puo' avere piu' di un
    // tasto (`GoTo` ne ha due): `ShortcutFor` da' il primo, `LabelFor` li scrive
    // tutti, perche' e' quello che serve accanto a una voce di menu.
    Shortcut    ShortcutFor(int command) const;
    std::string LabelFor(int command) const;

    // Assegna |key| al comando, togliendolo a chi lo aveva: due comandi sullo stesso
    // tasto sarebbero un tasto che fa due cose a seconda di chi guarda.
    void Rebind(int command, const Shortcut& key);
    // Toglie il tasto a un comando, senza dargliene un altro.
    void Clear(int command);
    // I tasti di partenza, come all'avvio.
    void Reset();

    // La tabella, in ordine: (comando, tasto). La legge chi la mostra e chi la salva.
    const std::vector<std::pair<int, Shortcut>>& Bindings() const { return bindings_; }

    // "Ctrl+Maiusc+P", "Canc", "F4". Vuota per "nessun tasto".
    static std::string Label(const Shortcut& key);

    // Vero se il tasto si puo' **assegnare**: un modificatore da solo no, un tasto
    // di cui non si sa scrivere il nome nemmeno, e nemmeno un tasto da scrivere
    // senza modificatori — una lettera sciolta la legge la casella di testo, e un
    // comando la' renderebbe impossibile scrivere una parola che la contiene.
    static bool Assignable(const Shortcut& key);

private:
    std::vector<std::pair<int, Shortcut>> bindings_;
};
} // namespace Bolt
