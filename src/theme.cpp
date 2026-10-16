// ---------------------------------------------------------------------------
// Le due tavolozze, e la preferenza del sistema.
//
// Questo file sono **costanti**. Cambiarne una e' cambiare un colore, e nient'altro
// nel programma se ne accorge: chi disegna legge `app.theme`. E' la ragione per cui
// il tema chiaro e' cablaggio e non disegno — i ruoli c'erano gia'.
//
// La tavolozza chiara **non e' la scura invertita**. Sono due tavolozze pensate
// ognuna per il proprio fondo: sul chiaro l'accento deve essere piu' profondo (un
// azzurro chiaro su bianco non si legge), la selezione e' un riempimento chiaro con
// il testo scuro sopra invece del contrario, e le righe alterne si distinguono
// abbassando il bianco, non alzando il nero. Invertire i valori avrebbe dato un
// risultato che si vede subito: testo grigio su grigio e un accento invisibile.
// ---------------------------------------------------------------------------
#include "theme.h"

#include <windows.h>

namespace Bolt
{

Theme MakeTheme(ColorScheme scheme)
{
    if (scheme == ColorScheme::Dark)
        return Theme{};   // i valori di partenza della struttura sono la tavolozza scura

    Theme light;
    // Il fondo e i pannelli. La finestra e' appena piu' scura dei pannelli: e' cosi'
    // che si vede dove finisce un pannello e comincia la finestra, senza un bordo.
    light.windowBg    = Color::FromHex(0xF2F3F5);
    light.panelBg     = Color::FromHex(0xFFFFFF);
    light.panelBorder = Color::FromHex(0xCFD1D6);
    // La banda delle intestazioni e delle sezioni: piu' scura del pannello, come
    // nella tavolozza scura e' piu' chiara. Il verso cambia, il ruolo no.
    light.headerBg    = Color::FromHex(0xE9EBEF);
    light.rowOdd      = Color::FromHex(0xF6F7F9);
    // La selezione e' un **riempimento chiaro** con il testo scuro sopra: sul chiaro
    // il contrario (riempimento scuro, testo bianco) salterebbe fuori dalla lista
    // come un buco nero.
    light.rowSelected = Color::FromHex(0xCBE3F8);
    light.rowHover    = Color::FromHex(0xE8F1FB);
    light.text        = Color::FromHex(0x1B1C1F);
    light.textDim     = Color::FromHex(0x5C6069);
    // Piu' profondo di quello scuro, che sul bianco si sgrana: 0x4C8DFF su bianco e'
    // un azzurro chiaro che non si legge ne' come selezione ne' come bordo.
    light.accent      = Color::FromHex(0x1F66C8);
    light.scrollThumb = Color::FromHex(0xB3B6BC);
    light.separator   = Color::FromHex(0xDCDEE3);

    // Dietro un'immagine: nella scura e' piu' scuro del pannello, nella chiara e'
    // appena piu' scuro del bianco. Serve a dire dove finisce la foto.
    light.previewFrame  = Color::FromHex(0xEDEEF1);
    // I segni che non sono testo — la freccia di tabulazione e il punto dello spazio:
    // in tono spento, com'e' giusto per qualcosa che si deve vedere e non leggere.
    light.whitespaceInk = Color::FromHex(0xA8ABB2);
    light.volumeTrack   = Color::FromHex(0xDCDEE3);
    light.warning       = Color::FromHex(0xC4442C);
    // Il rosso del pulsante di chiusura e' **quello di Windows**, uguale nelle due
    // tavolozze: e' il colore con cui il sistema dice "questa chiude la finestra", e
    // non e' un colore del nostro tema.
    light.closeHover    = Color::FromHex(0xC42B1C);
    // La voce della cartella aperta: piu' scura del fondo della barra, come nella
    // tavolozza scura e' piu' chiara del suo.
    light.sidebarActive = Color::FromHex(0xDFE1E6);
    // L'ombra, sul chiaro, e' molto piu' tenue: intorno a un pannello bianco un nero
    // al 45% si legge come un bordo scuro, non come qualcosa che sta sopra.
    light.shadow = Color{0.0f, 0.0f, 0.0f, 0.16f};
    return light;
}

bool SystemPrefersLightScheme()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    DWORD value = 0;
    DWORD size  = sizeof(value);
    DWORD type  = 0;
    const LONG read = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                       reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);

    if (read != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value))
        return false;
    return value != 0;
}

} // namespace Bolt
