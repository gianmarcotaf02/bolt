#include "image_preview.h"

#include <wincodec.h>

namespace ImagePreview
{
namespace
{

// Il ridimensionamento lo fa WIC: e' quello che sa farlo bene, e farlo a mano
// significherebbe scrivere un filtro peggiore.
bool ScaleToFit(IWICImagingFactory* factory, IWICBitmapSource* source, int maxSide, Image* out)
{
    UINT width = 0;
    UINT height = 0;
    if (FAILED(source->GetSize(&width, &height)) || width == 0 || height == 0)
        return false;

    const double scale = static_cast<double>(maxSide) / static_cast<double>((std::max)(width, height));
    const UINT targetWidth = static_cast<UINT>((std::max)(1.0, width * scale));
    const UINT targetHeight = static_cast<UINT>((std::max)(1.0, height * scale));

    IWICBitmapScaler* scaler = nullptr;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) || scaler == nullptr)
        return false;

    IWICFormatConverter* converter = nullptr;
    HRESULT result = E_FAIL;
    if (SUCCEEDED(scaler->Initialize(source, targetWidth, targetHeight,
                                     WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) && converter != nullptr)
    {
        result = converter->Initialize(scaler, GUID_WICPixelFormat32bppRGBA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
    }

    if (SUCCEEDED(result))
    {
        out->width  = static_cast<int>(targetWidth);
        out->height = static_cast<int>(targetHeight);
        out->rgba.assign(static_cast<size_t>(targetWidth) * targetHeight * 4, 0);
        result = converter->CopyPixels(nullptr, targetWidth * 4,
                                       static_cast<UINT>(out->rgba.size()), out->rgba.data());
        if (FAILED(result))
            out->rgba.clear();
    }

    if (converter != nullptr)
        converter->Release();
    scaler->Release();
    return SUCCEEDED(result);
}

} // namespace

Image Load(const std::wstring& path, int maxSide)
{
    Image image;
    if (path.empty() || maxSide <= 0)
        return image;

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        factory == nullptr)
        return image;

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &decoder)) &&
        decoder != nullptr && SUCCEEDED(decoder->GetFrame(0, &frame)) && frame != nullptr)
        ScaleToFit(factory, frame, maxSide, &image);

    if (frame != nullptr)
        frame->Release();
    if (decoder != nullptr)
        decoder->Release();
    factory->Release();
    return image;
}

} // namespace ImagePreview
