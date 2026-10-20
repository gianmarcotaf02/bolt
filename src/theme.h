// ---------------------------------------------------------------------------
// Il tema: i ruoli di colore che l'interfaccia legge, e le due tavolozze.
//
// Lo spec del riferimento (`docs/03` §2) elenca **35 token** di colore e due
// varianti, chiara e scura (`ColorSchemeLight`, `ColorSchemeDark`), piu' la scelta
// `SystemColorScheme`, che segue il sistema invece di deciderlo.
//
// Qui ci sono i token che hanno **un lettore**, e ce l'hanno tutti: ogni campo di
// `Theme` e' letto da almeno un punto del disegno. I token dello spec che ancora non
// hanno un lettore sono elencati in fondo, e restano fuori invece di diventare campi
// scritti e mai letti — che e' il difetto silenzioso di `AGENTS.md` §6. "Un campo
// scritto e mai letto si cerca nei **lettori**", e un token che nessuno legge non e'
// un tema: e' un colore in attesa.
//
//     Con un lettore:        Background (windowBg), Surface (panelBg), Border
//                            (panelBorder), Outline (headerBg), AlternatingRow
//                            (rowOdd), ContentSelection (rowSelected), ContentHover
//                            (rowHover), Text (text), Secondary (textDim),
//                            Selection (accent), Hover (scrollThumb), Separator
//                            (separator), Inner (previewFrame), Disabled
//                            (whitespaceInk), Progress (volumeTrack), Warning
//                            (warning), Caption (closeHover) — piu' quello della
//                            barra laterale per la cartella aperta (sidebarActive), e
//                            l'ombra, che e' nostra e non dello spec. `Progress` era
//                            una barra di disco sola; adesso e' anche la barra del
//                            pannello dell'operazione in corso, che e' il posto per
//                            cui lo spec lo prevedeva.
//
//     Senza lettore:         Clear, RectSelection, ContentDisabledSelection, File,
//                            Folder, Hidden, IconTint, Group, Match, MatchHover,
//                            MatchSelection, MatchDisabledSelection,
//                            OutlineSelection, OutlineHover, OutlineDisabledSelection,
//                            SurfaceSeparator, Foreground, ForegroundDisabled,
//                            InactiveOpacity.
//                            Sono i token di cose che il programma non disegna
//                            ancora: le icone tinte, gli stati di corrispondenza di
//                            un filtro. Ognuno diventa un campo il giorno in cui c'e'
//                            un posto che lo legge.
//
// Le due tavolozze stanno in `theme.cpp`: cambiarne una e' cambiare delle costanti,
// e il resto del programma non se ne accorge — legge `app.theme`, e nient'altro.
// ---------------------------------------------------------------------------
#pragma once

#include "ui_renderer.h"

namespace Bolt
{
// Quale delle due tavolozze. La terza possibilita' — "segui il sistema" — non e' un
// valore come questi: e' una **risposta che cambia** mentre il programma e' aperto,
// ed e' per questo che sta in `AppState` come interruttore (`systemColorScheme`) e
// non qui.
enum class ColorScheme { Dark, Light };

struct Theme
{
    // I dodici ruoli di sempre, quelli che il disegno legge da M1.
    Color windowBg    = Color::FromHex(0x1B1C1F);   // Background
    Color panelBg     = Color::FromHex(0x232428);   // Surface
    Color panelBorder = Color::FromHex(0x32343A);   // Border
    Color headerBg    = Color::FromHex(0x1F2024);   // Outline: la banda, non una linea
    Color rowOdd      = Color::FromHex(0x26272C);   // AlternatingRow
    Color rowSelected = Color::FromHex(0x2F4C7E);   // ContentSelection
    Color rowHover    = Color::FromHex(0x2C2E34);   // ContentHover
    Color text        = Color::FromHex(0xE6E7EA);   // Text
    Color textDim     = Color::FromHex(0x9195A0);   // Secondary
    Color accent      = Color::FromHex(0x4C8DFF);   // Selection
    Color scrollThumb = Color::FromHex(0x4A4D55);   // Hover
    Color separator   = Color::FromHex(0x2A2C31);   // Separator

    // I sei che erano numeri scritti a mano accanto al disegno, e che il tema chiaro
    // avrebbe lasciato scuri in mezzo a un'interfaccia chiara. Sono token come gli
    // altri: adesso hanno un nome, e una tavolozza li riempie.
    Color previewFrame   = Color::FromHex(0x131417);   // Inner: dietro un'immagine
    Color whitespaceInk  = Color::FromHex(0x555B66);   // Disabled: `->` e `·` dei testi
    Color volumeTrack    = Color::FromHex(0x2A2D34);   // Progress: la barra di un disco,
                                                        // e quella dell'operazione in corso
    Color warning        = Color::FromHex(0xE06C5A);   // Warning: disco quasi pieno
    Color closeHover     = Color::FromHex(0xC42B1C);   // Caption: il pulsante di chiusura
    Color sidebarActive  = Color::FromHex(0x2E3138);   // la voce della cartella aperta

    // L'ombra dei pannelli che si aprono sopra il resto — palette, menu, suggerimenti.
    // **Non e' un token dello spec**: i 35 non ne hanno uno, perche' l'ombra la
    // disegna il sistema per le finestre vere, e qui i pannelli sono quadrilateri nello
    // stesso fotogramma. Ce l'ha un ruolo, ed e' un ruolo che il tema chiaro deve poter
    // cambiare: un'ombra al 45% di nero attorno a un menu chiaro non e' un'ombra, e' un
    // livido.
    Color shadow = Color{0.0f, 0.0f, 0.0f, 0.45f};
};

// La tavolozza di uno schema.
Theme MakeTheme(ColorScheme scheme);

// Il nome di uno schema come si legge — la **stessa** stringa per il pannello delle
// opzioni, la palette e la scelta, che la rilegge invece di scriverne una seconda.
const char* SchemeLabel(ColorScheme scheme);

// Quale tavolozza si sta usando. "Segui il sistema" non e' una tavolozza: e' una
// domanda, e si rifa' ogni volta che serve, cosi' vale anche quando la preferenza
// del sistema cambia a finestra aperta.
struct AppState;
ColorScheme SchemeInUse(const AppState& app);

// Applica la tavolozza **e** allinea il riquadro di DWM: il colore dentro la
// finestra e quello che il sistema le disegna attorno sono la stessa decisione.
// Va chiamata quando la finestra esiste; prima allinea solo la tavolozza.
void ApplyTheme(AppState& app);

// Scegliere una tavolozza dalla palette o dalle opzioni: si applica subito, si
// **smette** di seguire il sistema e si ricorda. L'esito lo dice chi chiama, perche'
// dalla palette il pannello si chiude e senza una riga di stato non si saprebbe cosa
// e' cambiato.
void SetColorScheme(AppState& app, ColorScheme scheme);

// Il sistema preferisce il chiaro? E' `AppsUseLightTheme`, in
// `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`.
//
// Se la chiave non c'e' — un Windows che non ha mai registrato una preferenza — la
// risposta e' **no**, cioe' scuro: e' il valore di partenza di questo programma, e
// un'applicazione che si sveglia chiara su una macchina che non ha detto niente
// sarebbe un cambio che nessuno ha chiesto.
bool SystemPrefersLightScheme();
} // namespace Bolt
