// ---------------------------------------------------------------------------
// Backend Direct3D 11.
//
// E' la seconda implementazione di `IRenderer`: stesso batcher di quadrilateri,
// stesso formato di vertice (30 float), stessi shader — riscritti in HLSL perche'
// D3D non legge il GLSL. Chi disegna non cambia una riga.
//
// Differenze dai due lati che contano:
//
//   * `SV_Position` nel pixel shader e' gia' in pixel con l'origine in **alto a
//     sinistra**, +Y in giu', come lo spazio dell'interfaccia: il ribaltamento che
//     serve in OpenGL qui non serve.
//   * `SV_VertexID`, con una draw call indicizzata, e' l'indice letto dal buffer,
//     esattamente come `gl_VertexID`: i bit bassi selezionano l'angolo.
//   * il ritaglio e' top-left come il nostro `Rect`, quindi niente conversione.
//
// La scelta a runtime e il fallback stanno in `main.cpp`: qui c'e' solo come si
// disegna, non quando.
// ---------------------------------------------------------------------------
#pragma once

#include "ui_renderer.h"

#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

class D3D11Renderer final : public IRenderer
{
public:
    explicit D3D11Renderer(HWND window);
    ~D3D11Renderer() override;

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    const char* Name() const override { return "Direct3D 11"; }

    bool Initialize(std::string* error) override;
    void Shutdown() override;

    bool CreateTextureArray(int layers, int size, std::string* error) override;
    void UploadLayerRegion(int layer, const uint8_t* rgba, int width, int height,
                           int offsetX, int offsetY) override;

    void BeginFrame(int width, int height) override;
    void Present() override;

    void Begin(int viewportWidth, int viewportHeight) override;
    void Draw(const UIDraw& primitive) override;
    void FillRect(Rect rect, Color color, float radius = 0.0f) override;
    void BorderRect(Rect rect, Color color, float width, float radius = 0.0f) override;
    void End() override;
    size_t QuadCount() const override;
    void ReserveQuads(size_t quads) override;

    void SetScissor(Rect rect) override;
    void ClearScissor() override;

private:
    void AppendQuad(const UIDraw& primitive);
    void ReleaseBackbuffer();
    bool  EnsureBackbuffer(int width, int height, std::string* error);

    HWND window_ = nullptr;

    ID3D11Device*           device_  = nullptr;
    ID3D11DeviceContext*    context_ = nullptr;
    IDXGISwapChain*         swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;

    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader*  pixelShader_  = nullptr;
    ID3D11InputLayout*  inputLayout_  = nullptr;
    ID3D11Buffer*       constantBuffer_ = nullptr;
    ID3D11BlendState*      blendState_  = nullptr;
    ID3D11RasterizerState* rasterState_ = nullptr;
    ID3D11SamplerState*    sampler_     = nullptr;

    ID3D11Texture2D*          texture_     = nullptr;
    ID3D11ShaderResourceView* textureView_ = nullptr;

    ID3D11Buffer* vertexBuffer_ = nullptr;
    ID3D11Buffer* indexBuffer_  = nullptr;

    size_t vertexCapacityBytes_ = 0;
    size_t indexCapacityBytes_  = 0;

    std::vector<float>    vertices_;
    std::vector<uint32_t> indices_;

    int  viewportWidth_  = 0;
    int  viewportHeight_ = 0;
    bool mipsDirty_      = false;

    D3D11_VIEWPORT viewport_{};
    D3D11_RECT     scissor_{};
    bool           scissorSet_ = false;
};
