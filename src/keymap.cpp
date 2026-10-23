// ---------------------------------------------------------------------------
// La mappa dei tasti — implementazione.
//
// La tabella dei tasti di partenza sta qui sotto, ed e' l'unico posto in cui le
// scorciatoie sono scritte: prima stavano dentro i `case` di `WindowProc` (`case 'C':
// if (ctrl) ...`), dove il tasto **era** il codice e non si poteva cambiare senza
// ricompilare. Adesso e' un dato, e l'input lo interroga invece di conoscerlo.
//
// L'ordine delle righe e' quello con cui si leggono in un elenco: prima i file, poi
// le schede, poi la navigazione e l'aspetto — non l'ordine del vecchio switch.
// ---------------------------------------------------------------------------
#include "keymap.h"

#include "app_state.h"
#include "app_ui.h"
#include "language.h"

#include <windows.h>

#include <algorithm>

namespace Bolt
{
namespace
{
// Un tasto senza modificatori. La comodita' di scrivere le righe della tabella.
Shortcut Key(unsigned key) { return Shortcut{key, false, false, false}; }
Shortcut Ctrl(unsigned key) { return Shortcut{key, true, false, false}; }
Shortcut CtrlShift(unsigned key) { return Shortcut{key, true, true, false}; }
Shortcut Shift(unsigned key) { return Shortcut{key, false, true, false}; }

// Il nome di un tasto come lo scrive una persona. Le lettere e le cifre sono se
// stesse; i pochi che hanno un nome — `Canc`, `F4` — hanno il nome, tradotto quando
// serve: "Maiusc" per un italiano, "Shift" per tutti gli altri. Gli altri li sa
// Windows, e glieli si chiede: vedi in fondo.
std::string KeyName(unsigned key)
{
    if (key >= '0' && key <= '9')
        return std::string(1, static_cast<char>(key));
    if (key >= 'A' && key <= 'Z')
        return std::string(1, static_cast<char>(key));
    if (key >= VK_F1 && key <= VK_F24)
        return "F" + std::to_string(key - VK_F1 + 1);

    switch (key)
    {
        case VK_DELETE:   return Tr("Canc", "Del");
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return Tr("PagSu", "PgUp");
        case VK_NEXT:     return Tr("PagGiu", "PgDn");
        case VK_ESCAPE:   return "Esc";
        case VK_RETURN:   return "Invio";
        case VK_SPACE:    return Tr("Spazio", "Space");
        case VK_TAB:      return "Tab";
        case VK_BACK:     return "Backspace";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_PLUS:  return "+";
        case VK_OEM_MINUS: return "-";
        case VK_APPS:     return Tr("Menu", "Menu");
        default:          break;
    }

    // Tutti gli altri — `;`, `.`, i tasti del tastierino, i tasti multimediali — hanno
    // un nome che lo sa Windows, e lo dice **nella lingua della tastiera in uso**. Il
    // nome non e' un vezzo: un tasto senza nome non si puo' assegnare (`Assignable`),
    // quindi senza questa domanda la mappa avrebbe dei buchi travestiti da rifiuti.
    //
    // Il bit 24 di `lParam` dichiara che il tasto e' **esteso**: `Insert`, `Canc`,
    // `Home`, `Fine` e il tastierino hanno due codici di scansione, e senza quel bit
    // il nome sarebbe quello dell'altro tasto — `Insert` darebbe "0". Un nome
    // sbagliato e' peggio di nessun nome, quindi si dichiara invece di sperare.
    const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (scan == 0)
        return std::string();

    const bool extended =
        key == VK_INSERT || key == VK_DELETE || key == VK_HOME || key == VK_END ||
        key == VK_PRIOR || key == VK_NEXT || key == VK_LEFT || key == VK_RIGHT ||
        key == VK_UP || key == VK_DOWN || key == VK_NUMLOCK || key == VK_DIVIDE ||
        key == VK_SNAPSHOT || key == VK_APPS || key == VK_LWIN || key == VK_RWIN ||
        key == VK_RCONTROL || key == VK_RMENU || key == VK_RETURN;
    const LPARAM lparam = static_cast<LPARAM>((scan << 16) | (extended ? (1u << 24) : 0u));

    wchar_t name[64] = {};
    if (GetKeyNameTextW(static_cast<LONG>(lparam), name, 64) <= 0)
        return std::string();
    return ToUtf8(name);
}

// Un tasto che, senza modificatori, serve a **scrivere**: produce un carattere, o
// cancella l'ultimo. Sono quelli che il filtro e la rinomina leggono, quindi
// assegnarne uno sciolto a un comando vorrebbe dire non poter piu' scrivere quella
// lettera. Con un modificatore la domanda non si pone: `Ctrl+C` non arriva mai al
// testo, e infatti si assegna.
bool TypingKey(unsigned key)
{
    if (key == VK_SPACE || key == VK_BACK)
        return true;
    if (key >= '0' && key <= '9')
        return true;
    if (key >= 'A' && key <= 'Z')
        return true;

    // I segni e la punteggiatura: **quali** sono lo decide la tastiera in uso — `ò`
    // per un italiano, `;` per un inglese — e il carattere che Windows associa al
    // tasto e' l'unica risposta che non va tradotta a mano. Guardare il nome non
    // basta: "è" sono due byte, e contare i caratteri dello UTF-8 avrebbe detto che
    // un tasto da scrivere non lo e'.
    if (key >= VK_OEM_1 && key <= VK_OEM_102)
        return MapVirtualKeyW(key, MAPVK_VK_TO_CHAR) >= 0x20;
    return false;
}
} // namespace

std::string KeyMap::Label(const Shortcut& key)
{
    const std::string name = KeyName(key.key);
    if (name.empty())
        return std::string();

    std::string text;
    if (key.ctrl)
        text += "Ctrl+";
    if (key.alt)
        text += "Alt+";
    if (key.shift)
        text += std::string(Tr("Maiusc", "Shift")) + "+";
    return text + name;
}

bool KeyMap::Assignable(const Shortcut& key)
{
    // I modificatori da soli non sono tasti da assegnare, e i tasti con un nome che
    // non ne hanno uno da mostrare nemmeno: assegnarne uno darebbe una riga vuota
    // nell'elenco, e un tasto che non si sa di avere. Le tre lettere di stato
    // (`Stamp`, `Bloc Num`, `Bloc Scorr`) e `Pausa` fanno parte della stessa
    // famiglia: si preme senza volerlo, e non fa niente.
    switch (key.key)
    {
        case VK_CONTROL: case VK_SHIFT: case VK_MENU:
        case VK_LCONTROL: case VK_RCONTROL: case VK_LSHIFT: case VK_RSHIFT:
        case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL: case VK_PAUSE:
            return false;
        default:
            break;
    }
    if (KeyName(key.key).empty())
        return false;

    // Un tasto da **scrivere** sciolto non si assegna: senza `Ctrl` e senza `Alt`
    // arriva al testo, quindi un comando su `C` renderebbe impossibile scrivere una
    // parola che contiene una `c`. `Maiusc` non conta come modificatore: `Maiusc+C`
    // **e'** una `C` maiuscola.
    if (!key.ctrl && !key.alt && TypingKey(key.key))
        return false;
    return true;
}

KeyMap::KeyMap()
{
    Reset();
}

void KeyMap::Reset()
{
    bindings_ = {
        // File: le quattro operazioni che si fanno di continuo. `Canc` per il
        // cestino e `Maiusc+Canc` per la definitiva: l'eliminazione che non si
        // annulla deve restare scomoda.
        {CmdCopy,            Ctrl('C')},
        {CmdCut,             Ctrl('X')},
        {CmdPaste,           Ctrl('V')},
        {CmdDelete,          Key(VK_DELETE)},
        {CmdDeletePermanent, Shift(VK_DELETE)},
        {CmdRename,          Key(VK_F2)},
        {CmdNewFolder,       CtrlShift('N')},

        // Schede.
        {CmdNewTab,          Ctrl('T')},
        {CmdCloseTab,        Ctrl('W')},
        // I due pannelli: `Ctrl+S` affianca, `Ctrl+Shift+S` divide in orizzontale. Il
        // tasto piu' comodo va a quello che si usa di piu' — affiancare due cartelle e'
        // il gesto di tutti i giorni.
        {CmdSplitRight,      CtrlShift('S')},
        {CmdSplitBottom,     Ctrl('S')},

        // Le sottocartelle dentro la lista: `Ctrl+E` e' il tasto che il riferimento
        // da' a `Toggle Subfolders`.
        {CmdToggleSubfolders, Ctrl('E')},

        {CmdNextTab,         Ctrl(VK_TAB)},
        {CmdPalette,         CtrlShift('P')},

        // Navigazione e lettura. `GoTo` ha due tasti: il riferimento li ha tutti e
        // due, e chi ne ricorda uno solo non deve sapere l'altro.
        {CmdRefresh,         Key(VK_F5)},
        {CmdGoTo,            Ctrl('P')},
        {CmdGoTo,            Key(VK_F4)},

        // Le quattro viste: prima erano quattro `case` con un `Ctrl+1..4` scritto a
        // mano dentro, ed erano l'unica via col puntatore oltre alla barra in fondo.
        {CmdViewDetails,     Ctrl('1')},
        {CmdViewList,        Ctrl('2')},
        {CmdViewTiles,       Ctrl('3')},
        {CmdViewIcons,       Ctrl('4')},

        // L'aspetto e le opzioni.
        {CmdZoomIn,          Ctrl(VK_OEM_PLUS)},
        {CmdZoomOut,         Ctrl(VK_OEM_MINUS)},
        {CmdOpenOptions,     Ctrl(VK_OEM_COMMA)},
    };
}

int KeyMap::CommandFor(unsigned key, bool ctrl, bool shift, bool alt) const
{
    for (const auto& binding : bindings_)
    {
        if (binding.second.key == key && binding.second.ctrl == ctrl &&
            binding.second.shift == shift && binding.second.alt == alt)
            return binding.first;
    }
    return -1;
}

Shortcut KeyMap::ShortcutFor(int command) const
{
    for (const auto& binding : bindings_)
    {
        if (binding.first == command)
            return binding.second;
    }
    return Shortcut{};
}

std::vector<std::string> KeyMap::LabelsFor(int command) const
{
    std::vector<std::string> labels;
    for (const auto& binding : bindings_)
    {
        if (binding.first != command)
            continue;
        const std::string label = Label(binding.second);
        if (!label.empty())
            labels.push_back(label);
    }
    return labels;
}

std::string KeyMap::LabelFor(int command) const
{
    std::string text;
    for (const std::string& label : LabelsFor(command))
    {
        if (!text.empty())
            text += std::string(Tr(" o ", " or "));
        text += label;
    }
    return text;
}

void KeyMap::Rebind(int command, const Shortcut& key)
{
    // Si ricostruisce la tabella invece di modificarla sul posto: l'ordine resta
    // quello di prima — il comando si mette **dove stava** la sua prima riga — e le
    // due regole si vedono tutte e due a colpo d'occhio. Il tasto si toglie a chi lo
    // aveva (due comandi sullo stesso tasto sarebbero un tasto che fa due cose), e di
    // un comando con piu' tasti ne resta **uno**, perche' rimappare vuol dire
    // "questo", non "anche questo".
    const auto same = [](const Shortcut& a, const Shortcut& b) {
        return a.key == b.key && a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt;
    };

    std::vector<std::pair<int, Shortcut>> next;
    next.reserve(bindings_.size() + 1);

    bool written = false;
    for (const std::pair<int, Shortcut>& binding : bindings_)
    {
        if (!key.empty() && binding.first != command && same(binding.second, key))
            continue;

        if (binding.first == command)
        {
            if (written)
                continue;
            written = true;
            if (!key.empty())
                next.push_back({command, key});
            continue;
        }
        next.push_back(binding);
    }

    // Un comando che non aveva tasti: la riga si aggiunge in fondo, che e' dove
    // finisce quello che si aggiunge.
    if (!written && !key.empty())
        next.push_back({command, key});

    bindings_.swap(next);
}

void KeyMap::Clear(int command)
{
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [&](const std::pair<int, Shortcut>& binding) {
                                       return binding.first == command;
                                   }),
                    bindings_.end());
}
} // namespace Bolt
