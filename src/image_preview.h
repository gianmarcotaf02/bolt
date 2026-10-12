// ---------------------------------------------------------------------------
// Un'immagine decodificata, per l'anteprima grande dell'inspector.
//
// La miniatura della shell arriva a 128 px: va bene per un'icona, non per
// guardare una fotografia. Qui si decodifica il file davvero — WIC legge quello
// che Windows sa leggere, quindi JPEG, PNG, BMP, GIF, TIFF e i formati dei codec
// installati — e si ridimensiona per stare in un quadrato di lato noto, che e'
// quello che lo strato di texture puo' contenere.
//
// Si conserva **il rapporto**, e si ridimensiona una volta sola qui: ingrandire e
// rimpicciolire nell'inspector e' poi solo geometria di un quad.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ImagePreview
{
struct Image
{
    int                   width = 0;    // dopo il ridimensionamento
    int                   height = 0;
    std::vector<uint8_t>  rgba;         // width * height * 4

    bool Valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Decodifica |path| ridimensionandola per stare in un quadrato di lato |maxSide|,
// conservando il rapporto. Un file che non e' un'immagine, o piu' grande di quanto
// WIC voglia decodificare, da' un'immagine non valida.
Image Load(const std::wstring& path, int maxSide);
} // namespace ImagePreview
