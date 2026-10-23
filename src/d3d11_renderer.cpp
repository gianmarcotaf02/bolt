#include "d3d11_renderer.h"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>

namespace {

// Il vertice e' lo stesso dell'OpenGL: 34 float, nello stesso ordine. Cambia solo
// chi lo legge, quindi il conto dei float deve restare in un posto solo — qui, e
// in `ui_renderer.cpp` per l'altro backend.
constexpr size_t kFloatsPerVertex = 34;
constexpr size_t kVerticesPerQuad = 4;
constexpr size_t kIndicesPerQuad  = 6;

// Gli shader. La logica e' quella di `src/shaders/ui.*`, riscritta in HLSL: stessa
// SDF, stesso bordo, stessa ombra, stesso dash. Non e' una seconda implementazione
// *visiva*, e' la stessa detta in un'altra lingua.
const char* const kVertexShader = R"(
cbuffer Constants : register(b0) { row_major float4x4 uProjection; };

struct VSInput
{
    float2 cbl : TEXCOORD0;
    float2 cbr : TEXCOORD1;
    float2 ctl : TEXCOORD2;
    float2 ctr : TEXCOORD3;
    float2 uvMin : TEXCOORD4;
    float2 uvMax : TEXCOORD5;
    float  layer : TEXCOORD6;
    float4 color : TEXCOORD7;
    float4 gradient : TEXCOORD8;
    float4 radius : TEXCOORD9;
    float  strokeWidth : TEXCOORD10;
    float  softness : TEXCOORD11;
    float  dashOn : TEXCOORD12;
    float  dashOff : TEXCOORD13;
    float  flags : TEXCOORD14;
    float4 clipRect : TEXCOORD15;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 shapeMin : TEXCOORD0;
    float2 shapeSize : TEXCOORD1;
    float4 radius : TEXCOORD2;
    float  strokeWidth : TEXCOORD3;
    float  softness : TEXCOORD4;
    float  dashOn : TEXCOORD5;
    float  dashOff : TEXCOORD6;
    float  flags : TEXCOORD7;
    float2 uv : TEXCOORD8;
    float  layer : TEXCOORD9;
    float4 color : TEXCOORD10;
    float4 gradient : TEXCOORD11;
    // `nointerpolation`: come `flat` nel GLSL. Il rettangolo e' lo stesso nei
    // quattro vertici del quad, e interpolarlo sarebbe un arrotondamento in piu'
    // proprio sul confronto che decide se un frammento esiste.
    nointerpolation float4 clipRect : TEXCOORD12;
};

VSOutput main(VSInput input, uint vertexId : SV_VertexID)
{
    // Con una draw call indicizzata `SV_VertexID` e' l'indice letto dal buffer,
    // non un contatore: i due bit bassi dicono l'angolo. E' la stessa nota di
    // `gl_VertexID`, e l'errore e' lo stesso se la si sbaglia.
    uint corner = vertexId & 3u;

    float2 position = (corner == 0u) ? input.cbl
                    : (corner == 1u) ? input.cbr
                    : (corner == 2u) ? input.ctl
                                     : input.ctr;

    float2 uv = (corner == 0u) ? float2(input.uvMin.x, input.uvMax.y)
              : (corner == 1u) ? input.uvMax
              : (corner == 2u) ? input.uvMin
                               : float2(input.uvMax.x, input.uvMin.y);

    VSOutput output;
    output.pos = mul(float4(position, 0.0, 1.0), uProjection);

    output.shapeMin = min(min(input.cbl, input.cbr), min(input.ctl, input.ctr));
    output.shapeSize = max(max(input.cbl, input.cbr), max(input.ctl, input.ctr)) - output.shapeMin;

    output.radius = input.radius;
    output.strokeWidth = input.strokeWidth;
    output.softness = input.softness;
    output.dashOn = input.dashOn;
    output.dashOff = input.dashOff;
    output.flags = input.flags;

    output.uv = uv;
    output.layer = input.layer;
    output.color = input.color;
    output.gradient = input.gradient;
    output.clipRect = input.clipRect;
    return output;
}
)";

const char* const kPixelShader = R"(
Texture2DArray uTexture : register(t0);
SamplerState uSampler : register(s0);

struct VSOutput
{
    float4 pos : SV_Position;
    float2 shapeMin : TEXCOORD0;
    float2 shapeSize : TEXCOORD1;
    float4 radius : TEXCOORD2;
    float  strokeWidth : TEXCOORD3;
    float  softness : TEXCOORD4;
    float  dashOn : TEXCOORD5;
    float  dashOff : TEXCOORD6;
    float  flags : TEXCOORD7;
    float2 uv : TEXCOORD8;
    float  layer : TEXCOORD9;
    float4 color : TEXCOORD10;
    float4 gradient : TEXCOORD11;
    // `nointerpolation`: come `flat` nel GLSL. Il rettangolo e' lo stesso nei
    // quattro vertici del quad, e interpolarlo sarebbe un arrotondamento in piu'
    // proprio sul confronto che decide se un frammento esiste.
    nointerpolation float4 clipRect : TEXCOORD12;
};

static const float FLAG_TEXTURED = 1.0;
static const float FLAG_MONO     = 2.0;
static const float FLAG_TRIANGLE = 4.0;
static const float FLAG_TRIANGLE_DOWN = 8.0;
static const float FLAG_PLUS  = 16.0;
static const float FLAG_CROSS = 32.0;

// I cinque pulsanti di navigazione: `ui.frag` dice la ragione e queste sono le
// stesse forme, dette in HLSL. Una formula per forma, non due: se qui e li' 
// divergono, il backend Direct3D disegna qualcos'altro e nessuno se ne accorge
// finche' non guarda `BOLT_BACKEND=d3d11`.
static const float FLAG_ARROW       = 64.0;
static const float FLAG_ARROW_UP    = 128.0;
static const float FLAG_ARROW_LEFT  = 256.0;
static const float FLAG_REFRESH     = 512.0;
static const float FLAG_STAR        = 1024.0;
static const float FLAG_PIN         = 2048.0;

// Il triangolo della linguetta: la stessa formula dell'OpenGL, detta in HLSL.
// Per farlo puntare in giu' si ruota il punto, non si scrive una seconda formula.
// Distanza da un segmento con le punte tonde: la usano il triangolo e la croce.
//
// Il punto si chiama `p` e non `point`: `point` e' una **parola riservata** di HLSL
// (la primitiva dei geometry shader), e con quel nome il compilatore di Direct3D
// rifiutava l'intero pixel shader — che, non compilando, faceva ripiegare
// l'applicazione sull'OpenGL in silenzio. Vedi `docs/02` §7.52.
float segmentDistance(float2 p, float2 a, float2 b, float radius)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float along = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * along) - radius;
}

float triangleDistance(float2 p, float2 halfSize, float rounding)
{
    float width  = halfSize.x;
    float height = halfSize.y;

    float2 top    = float2(-width,  height);
    float2 tip    = float2( width,  0.0);
    float2 bottom = float2(-width, -height);

    float left = -width - p.x;

    float2 upperEdge = normalize(float2( height, 2.0 * width));
    float2 lowerEdge = normalize(float2( height, -2.0 * width));
    float upper = dot(p - top, upperEdge);
    float lower = dot(p - tip, lowerEdge);

    float planes = max(max(left, upper), lower);
    if (rounding <= 0.0)
        return planes;

    // Vedi ui.frag: gli spigoli si arrotondano con la distanza dal contorno.
    float edge = min(min(segmentDistance(p, top, tip, 0.0),
                         segmentDistance(p, tip, bottom, 0.0)),
                     segmentDistance(p, bottom, top, 0.0));
    return (planes < 0.0 ? -edge : edge) - rounding;
}

float roundedBoxDistance(float2 local, float2 halfSize, float4 radius)
{
    float r = local.x > 0.0
        ? (local.y > 0.0 ? radius.z : radius.y)
        : (local.y > 0.0 ? radius.w : radius.x);
    r = clamp(r, 0.0, min(halfSize.x, halfSize.y));
    float2 offset = abs(local) - halfSize + r;
    return min(max(offset.x, offset.y), 0.0) + length(max(offset, float2(0.0, 0.0))) - r;
}

// Il piu' e la croce: le stesse formule di ui.frag, dette in HLSL.
float plusDistance(float2 p, float2 halfSize, float thickness)
{
    float tip = thickness * 0.5;
    float2 horizontal = float2(max(halfSize.x - tip, 0.0), tip);
    float2 vertical   = float2(tip, max(halfSize.y - tip, 0.0));
    return min(roundedBoxDistance(p, horizontal, float4(tip, tip, tip, tip)),
               roundedBoxDistance(p, vertical, float4(tip, tip, tip, tip)));
}

float crossDistance(float2 p, float2 halfSize, float thickness)
{
    float tip = thickness * 0.5;
    float extent = max(min(halfSize.x, halfSize.y) - tip, 0.0);
    float backslash = segmentDistance(p, float2(-extent, -extent), float2(extent, extent), tip);
    float slash     = segmentDistance(p, float2(-extent,  extent), float2(extent, -extent), tip);
    return min(backslash, slash);
}

// Un triangolo pieno, vertici in senso antiorario (vedi `ui.frag`).
float filledTriangle(float2 p, float2 a, float2 b, float2 c, float rounding)
{
    float2 ab = normalize(float2(b.y - a.y, -(b.x - a.x)));
    float2 bc = normalize(float2(c.y - b.y, -(c.x - b.x)));
    float2 ca = normalize(float2(a.y - c.y, -(a.x - c.x)));

    float planes = max(max(dot(p - a, ab), dot(p - b, bc)), dot(p - c, ca));
    if (rounding <= 0.0)
        return planes;

    float edge = min(min(segmentDistance(p, a, b, 0.0), segmentDistance(p, b, c, 0.0)),
                     segmentDistance(p, c, a, 0.0));
    return (planes < 0.0 ? -edge : edge) - rounding;
}

// Un quadrilatero convesso, vertici in senso antiorario.
float quadDistance(float2 p, float2 a, float2 b, float2 c, float2 d)
{
    float2 n = normalize(float2(b.y - a.y, -(b.x - a.x)));
    float m = dot(p - a, n);
    n = normalize(float2(c.y - b.y, -(c.x - b.x)));
    m = max(m, dot(p - b, n));
    n = normalize(float2(d.y - c.y, -(d.x - c.x)));
    m = max(m, dot(p - c, n));
    n = normalize(float2(a.y - d.y, -(a.x - d.x)));
    return max(m, dot(p - d, n));
}

// La freccia: un'asta e una punta a 45°, tutte allo stesso spessore (vedi `ui.frag`).
float arrowDistance(float2 p, float2 halfSize, float thickness)
{
    float r = min(halfSize.x, halfSize.y);
    float t = max(thickness, 1.0) * 0.5;

    float2 tail = float2(-r + t, 0.0);
    float2 tip  = float2(r - t, 0.0);
    float shaft = segmentDistance(p, tail, tip, t);

    float arm = r * 0.66;
    float head = min(segmentDistance(p, tip, tip + float2(-arm,  arm), t),
                     segmentDistance(p, tip, tip + float2(-arm, -arm), t));

    return min(shaft, head);
}

// L'aggiorna: un anello aperto con una punta tangente dentro il taglio.
float refreshDistance(float2 p, float2 halfSize, float thickness)
{
    float r = min(halfSize.x, halfSize.y);
    float t = max(thickness, 1.0);
    float band = t * 0.5;
    float radius = r - band;
    float reach = length(p);

    float ring = abs(reach - radius) - band;

    float gapHalf = 0.70;
    float gap = (gapHalf - abs(atan2(p.y, p.x))) * max(reach, 0.001);

    float2 radial = float2(cos(gapHalf), sin(gapHalf));
    float2 tangent = float2(sin(gapHalf), -cos(gapHalf));
    float2 anchorHead = radial * radius;
    float head = filledTriangle(p, anchorHead + radial * t * 1.9, anchorHead - radial * t * 1.9,
                               anchorHead + tangent * t * 3.4, t * 0.1);

    return min(max(ring, gap), head);
}

// La stella: cinque punte; il segno dagli aquiloni, la distanza dai dieci lati
// presi come segmenti (vedi `ui.frag` per la ragione).
float starDistance(float2 p, float2 halfSize)
{
    float r = min(halfSize.x, halfSize.y);
    float inner = r * 0.32;
    float side = 0.6283185;
    float first = 1.5707963;

    float edge = 1e6;
    bool  inside = false;

    float2 vertex = float2(cos(first - side), sin(first - side)) * inner;
    for (int i = 0; i < 5; ++i)
    {
        float tipAngle = first + i * 1.2566371;
        float2 tip   = float2(cos(tipAngle), sin(tipAngle)) * r;
        float2 after = float2(cos(tipAngle + side), sin(tipAngle + side)) * inner;

        edge = min(edge, segmentDistance(p, vertex, tip, 0.0));
        edge = min(edge, segmentDistance(p, tip, after, 0.0));

        float2 before = vertex;
        if (quadDistance(p, float2(0.0, 0.0), after, tip, before) < 0.0)
            inside = true;

        vertex = after;
    }

    return inside ? -edge : edge;
}

// La puntina delle cartelle fissate: la stessa forma di `ui.frag`, detta in HLSL.
// Testa, piatto e punta, disegnati dritti e poi girati di 45 gradi ruotando il punto.
float pinDistance(float2 p, float2 halfSize)
{
    float unit = max(min(halfSize.x, halfSize.y), 1.0);
    float2 q = p / unit;

    float c = 0.70710678;
    q = float2(c * q.x - c * q.y, c * q.x + c * q.y);

    float head   = roundedBoxDistance(q - float2(0.0,  0.40), float2(0.34, 0.24), float4(0.12, 0.12, 0.12, 0.12));
    float flange = roundedBoxDistance(q - float2(0.0,  0.08), float2(0.66, 0.11), float4(0.06, 0.06, 0.06, 0.06));
    float needle = segmentDistance(q, float2(0.0, 0.08), float2(0.0, -0.98), 0.085);

    return min(min(head, flange), needle) * unit;
}

bool isOnDash(float2 frag, float2 shapeMin, float2 shapeSize, float strokeWidth,
              float dashOn, float dashOff)
{
    float period = dashOn + dashOff;
    if (period <= 0.0)
        return true;

    float2 local = frag - shapeMin;
    float w = shapeSize.x;
    float h = shapeSize.y;
    float along;

    if (local.y <= strokeWidth)          along = local.x;
    else if (local.x >= w - strokeWidth) along = w + local.y;
    else if (local.y >= h - strokeWidth) along = w + h + (w - local.x);
    else                                 along = 2.0 * w + h + (h - local.y);

    return fmod(along, period) < dashOn;
}

float4 main(VSOutput input) : SV_Target
{
    // `SV_Position`, nel pixel shader, e' gia' in pixel con l'origine in alto a
    // sinistra e +Y in giu': e' lo spazio dell'interfaccia, quindi niente
    // ribaltamento (che in OpenGL serve).
    float2 frag = input.pos.xy;

    // Il ritaglio arriva dal vertice, come in `src/shaders/ui.frag`: fuori dal
    // rettangolo il frammento non esiste. Non c'e' nessuno stato di scissor da
    // accendere, quindi il batch non va spezzato per ritagliare.
    if (frag.x < input.clipRect.x || frag.x >= input.clipRect.z ||
        frag.y < input.clipRect.y || frag.y >= input.clipRect.w)
        discard;

    float2 halfSize = input.shapeSize * 0.5;
    float2 centre   = input.shapeMin + halfSize;
    float2 local    = centre - frag;

    if ((input.dashOn + input.dashOff) > 0.0 &&
        !isOnDash(frag, input.shapeMin, input.shapeSize, input.strokeWidth, input.dashOn,
                  input.dashOff))
        discard;

    float4 color = input.color;
    if (fmod(input.flags, 2.0 * FLAG_TEXTURED) >= FLAG_TEXTURED)
    {
        float4 texel = uTexture.Sample(uSampler, float3(input.uv, input.layer));
        if (fmod(input.flags, 2.0 * FLAG_MONO) >= FLAG_MONO)
            texel = float4(texel.r, texel.r, texel.r, texel.r);
        color *= texel;
    }

    float2 relative = (frag - input.shapeMin) / max(input.shapeSize, float2(1.0, 1.0));
    float horizontal = lerp(input.gradient.x, input.gradient.y, relative.x);
    float vertical   = lerp(input.gradient.w, input.gradient.z, relative.y);
    color *= horizontal * vertical;

    float feather = max(input.softness, 1.0);
    float inset   = input.strokeWidth * 0.5;
    float4 radius = max(input.radius, float4(input.strokeWidth, input.strokeWidth,
                                            input.strokeWidth, input.strokeWidth));

    float2 innerHalf = max(halfSize - float2(inset + input.softness, inset + input.softness),
                           float2(0.0, 0.0));
    float distance;
    if (fmod(input.flags, 2.0 * FLAG_TRIANGLE) >= FLAG_TRIANGLE)
    {
        // Vedi ui.frag: in `local` i due assi sono girati, e un triangolo se ne
        // accorge anche se un rettangolo no.
        float2 upright = float2(-local.x, local.y);
        bool down = fmod(input.flags, 2.0 * FLAG_TRIANGLE_DOWN) >= FLAG_TRIANGLE_DOWN;
        float2 rotated = down ? float2(-upright.y, upright.x) : upright;
        distance = triangleDistance(rotated, innerHalf, input.radius.x);
    }
    else if (fmod(input.flags, 2.0 * FLAG_PLUS) >= FLAG_PLUS)
    {
        distance = plusDistance(local, innerHalf, max(input.radius.x, 1.0));
    }
    else if (fmod(input.flags, 2.0 * FLAG_CROSS) >= FLAG_CROSS)
    {
        distance = crossDistance(local, innerHalf, max(input.radius.x, 1.0));
    }
    else if (fmod(input.flags, 2.0 * FLAG_ARROW) >= FLAG_ARROW)
    {
        float2 upright = float2(-local.x, local.y);
        bool up   = fmod(input.flags, 2.0 * FLAG_ARROW_UP) >= FLAG_ARROW_UP;
        bool left = fmod(input.flags, 2.0 * FLAG_ARROW_LEFT) >= FLAG_ARROW_LEFT;
        if (up)
            upright = float2(upright.y, -upright.x);
        if (left)
            upright = -upright;
        distance = arrowDistance(upright, innerHalf, max(input.radius.x, 1.0));
    }
    else if (fmod(input.flags, 2.0 * FLAG_REFRESH) >= FLAG_REFRESH)
    {
        float2 upright = float2(-local.x, local.y);
        distance = refreshDistance(upright, innerHalf, max(input.radius.x, 1.0));
    }
    else if (fmod(input.flags, 2.0 * FLAG_STAR) >= FLAG_STAR)
    {
        distance = starDistance(local, innerHalf);
    }
    else if (fmod(input.flags, 2.0 * FLAG_PIN) >= FLAG_PIN)
    {
        float2 upright = float2(-local.x, local.y);
        distance = pinDistance(upright, innerHalf);
    }
    else
    {
        distance = roundedBoxDistance(local, innerHalf, radius);
    }

    float alpha;
    if (input.strokeWidth > 0.0)
        alpha = 1.0 - smoothstep(-feather * 0.5, feather * 0.5, abs(distance) - inset);
    else if (input.softness > 0.0)
        alpha = 1.0 - smoothstep(0.0, max(1.0, input.softness * 2.0), distance);
    else
        alpha = saturate(0.5 - distance);

    if (alpha <= 0.0)
        discard;

    return float4(color.rgb, color.a * alpha);
}
)";

bool CompileStage(const char* source, const char* entry, const char* target, std::string* error,
                  ID3DBlob** blob)
{
    ID3DBlob* messages = nullptr;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(source, std::strlen(source), "ui.hlsl", nullptr, nullptr, entry,
                                  target, flags, 0, blob, &messages);
    if (FAILED(hr))
    {
        if (error != nullptr)
        {
            char code[48];
            std::snprintf(code, sizeof(code), " (0x%08lX)", static_cast<unsigned long>(hr));
            *error = std::string("HLSL ") + entry + " failed to compile" + code + ":\n";
            if (messages != nullptr)
                *error += static_cast<const char*>(messages->GetBufferPointer());
            else
                *error += "(nessun messaggio dal compilatore)";
        }
        if (messages != nullptr)
            messages->Release();
        return false;
    }
    if (messages != nullptr)
        messages->Release();
    return true;
}

} // namespace

D3D11Renderer::D3D11Renderer(HWND window) : window_(window) {}

D3D11Renderer::~D3D11Renderer()
{
    // Le risorse si rilasciano in Shutdown(): qui il device puo' essere gia' via.
}

bool D3D11Renderer::Initialize(std::string* error)
{
    if (window_ == nullptr)
    {
        if (error != nullptr) *error = "Direct3D 11 needs a window";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferCount        = 2;
    swap.BufferDesc.Format  = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.OutputWindow       = window_;
    swap.SampleDesc.Count   = 1;
    swap.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;
    swap.Windowed           = TRUE;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL reached = D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &swap,
        &swapChain_, &device_, nullptr, &context_);
    if (FAILED(hr) || device_ == nullptr || swapChain_ == nullptr)
    {
        // Without the debug layer the same call can still succeed on a driver
        // that has no D3D11 at all: try WARP before giving up, so the fallback is
        // "slower", not "nothing".
        const HRESULT warp = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels,
            static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &swap,
            &swapChain_, &device_, &reached, &context_);
        if (FAILED(warp) || device_ == nullptr || swapChain_ == nullptr)
        {
            if (error != nullptr) *error = "Direct3D 11 device creation failed";
            Shutdown();
            return false;
        }
    }

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* pixelBlob  = nullptr;
    if (!CompileStage(kVertexShader, "main", "vs_5_0", error, &vertexBlob) ||
        !CompileStage(kPixelShader, "main", "ps_5_0", error, &pixelBlob))
    {
        if (vertexBlob != nullptr) vertexBlob->Release();
        if (pixelBlob != nullptr) pixelBlob->Release();
        Shutdown();
        return false;
    }

    if (FAILED(device_->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                           vertexBlob->GetBufferSize(), nullptr, &vertexShader_)) ||
        FAILED(device_->CreatePixelShader(pixelBlob->GetBufferPointer(),
                                          pixelBlob->GetBufferSize(), nullptr, &pixelShader_)))
    {
        if (error != nullptr) *error = "Direct3D 11 shader creation failed";
        vertexBlob->Release();
        pixelBlob->Release();
        Shutdown();
        return false;
    }

    // Lo stesso formato di vertice dell'OpenGL, attributo per attributo.
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"TEXCOORD", 0,  DXGI_FORMAT_R32G32_FLOAT,       0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1,  DXGI_FORMAT_R32G32_FLOAT,       0, 8,   D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2,  DXGI_FORMAT_R32G32_FLOAT,       0, 16,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3,  DXGI_FORMAT_R32G32_FLOAT,       0, 24,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4,  DXGI_FORMAT_R32G32_FLOAT,       0, 32,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 5,  DXGI_FORMAT_R32G32_FLOAT,       0, 40,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 6,  DXGI_FORMAT_R32_FLOAT,          0, 48,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 7,  DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 8,  DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 68,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 9,  DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 84,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 10, DXGI_FORMAT_R32_FLOAT,          0, 100, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 11, DXGI_FORMAT_R32_FLOAT,          0, 104, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 12, DXGI_FORMAT_R32_FLOAT,          0, 108, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 13, DXGI_FORMAT_R32_FLOAT,          0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 14, DXGI_FORMAT_R32_FLOAT,          0, 116, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 15, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 120, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const HRESULT inputResult = device_->CreateInputLayout(
        layout, static_cast<UINT>(sizeof(layout) / sizeof(layout[0])),
        vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), &inputLayout_);
    vertexBlob->Release();
    pixelBlob->Release();
    if (FAILED(inputResult))
    {
        if (error != nullptr) *error = "Direct3D 11 input layout creation failed";
        Shutdown();
        return false;
    }

    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = 16 * sizeof(float);
    constants.Usage     = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&constants, nullptr, &constantBuffer_)))
    {
        if (error != nullptr) *error = "Direct3D 11 constant buffer creation failed";
        Shutdown();
        return false;
    }

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable    = TRUE;
    blend.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blend, &blendState_)))
    {
        if (error != nullptr) *error = "Direct3D 11 blend state creation failed";
        Shutdown();
        return false;
    }

    // Il ritaglio non e' uno stato del rasterizzatore: e' un campo del vertice, e
    // a tagliare e' il pixel shader. Quindi niente `ScissorEnable` e niente
    // `RSSetScissorRects` da riportare a ogni disegno.
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    if (FAILED(device_->CreateRasterizerState(&raster, &rasterState_)))
    {
        if (error != nullptr) *error = "Direct3D 11 rasterizer state creation failed";
        Shutdown();
        return false;
    }

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sampler, &sampler_)))
    {
        if (error != nullptr) *error = "Direct3D 11 sampler creation failed";
        Shutdown();
        return false;
    }

    return true;
}

void D3D11Renderer::ReleaseBackbuffer()
{
    if (renderTarget_ != nullptr) { renderTarget_->Release(); renderTarget_ = nullptr; }
}

void D3D11Renderer::Shutdown()
{
    ReleaseBackbuffer();
    if (textureView_ != nullptr) { textureView_->Release(); textureView_ = nullptr; }
    if (texture_ != nullptr) { texture_->Release(); texture_ = nullptr; }
    if (vertexBuffer_ != nullptr) { vertexBuffer_->Release(); vertexBuffer_ = nullptr; }
    if (indexBuffer_ != nullptr) { indexBuffer_->Release(); indexBuffer_ = nullptr; }
    if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
    if (rasterState_ != nullptr) { rasterState_->Release(); rasterState_ = nullptr; }
    if (blendState_ != nullptr) { blendState_->Release(); blendState_ = nullptr; }
    if (constantBuffer_ != nullptr) { constantBuffer_->Release(); constantBuffer_ = nullptr; }
    if (inputLayout_ != nullptr) { inputLayout_->Release(); inputLayout_ = nullptr; }
    if (pixelShader_ != nullptr) { pixelShader_->Release(); pixelShader_ = nullptr; }
    if (vertexShader_ != nullptr) { vertexShader_->Release(); vertexShader_ = nullptr; }
    if (swapChain_ != nullptr) { swapChain_->Release(); swapChain_ = nullptr; }
    if (context_ != nullptr) { context_->Release(); context_ = nullptr; }
    if (device_ != nullptr) { device_->Release(); device_ = nullptr; }

    vertices_.clear();
    vertices_.shrink_to_fit();
    indices_.clear();
    indices_.shrink_to_fit();
    vertexCapacityBytes_ = 0;
    indexCapacityBytes_  = 0;
}

bool D3D11Renderer::CreateTextureArray(int layers, int size, std::string* error)
{
    if (layers <= 0 || size <= 0)
    {
        if (error != nullptr) *error = "texture array needs positive layer and size counts";
        return false;
    }

    if (textureView_ != nullptr) { textureView_->Release(); textureView_ = nullptr; }
    if (texture_ != nullptr) { texture_ = nullptr; }

    int levels = 1;
    for (int s = size; s > 1; s >>= 1)
        ++levels;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width      = static_cast<UINT>(size);
    desc.Height     = static_cast<UINT>(size);
    desc.MipLevels  = static_cast<UINT>(levels);
    desc.ArraySize  = static_cast<UINT>(layers);
    desc.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage      = D3D11_USAGE_DEFAULT;
    // `GenerateMips` vuole il bind come render target e il flag: senza, la catena
    // non si puo' generare e le icone rimpicciolite tornano a sgranarsi.
    desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags  = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_)))
    {
        if (error != nullptr) *error = "Direct3D 11 texture array creation failed";
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension                 = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray.MostDetailedMip = 0;
    view.Texture2DArray.MipLevels       = static_cast<UINT>(levels);
    view.Texture2DArray.FirstArraySlice = 0;
    view.Texture2DArray.ArraySize       = static_cast<UINT>(layers);
    if (FAILED(device_->CreateShaderResourceView(texture_, &view, &textureView_)))
    {
        if (error != nullptr) *error = "Direct3D 11 texture array view creation failed";
        return false;
    }

    mipsDirty_ = true;
    return true;
}

void D3D11Renderer::UploadLayerRegion(int layer, const uint8_t* rgba, int width, int height,
                                      int offsetX, int offsetY)
{
    if (texture_ == nullptr || rgba == nullptr || width <= 0 || height <= 0)
        return;

    D3D11_BOX box{};
    box.left   = static_cast<UINT>(offsetX);
    box.top    = static_cast<UINT>(offsetY);
    box.front  = 0;
    box.right  = static_cast<UINT>(offsetX + width);
    box.bottom = static_cast<UINT>(offsetY + height);
    box.back   = 1;

    int levels = 1;
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture_->GetDesc(&desc);
        levels = static_cast<int>(desc.MipLevels);
    }
    const UINT subresource = D3D11CalcSubresource(0, static_cast<UINT>(layer), static_cast<UINT>(levels));
    context_->UpdateSubresource(texture_, subresource, &box, rgba, static_cast<UINT>(width * 4), 0);
    mipsDirty_ = true;
}

bool D3D11Renderer::EnsureBackbuffer(int width, int height, std::string* error)
{
    if (swapChain_ == nullptr)
        return false;

    ReleaseBackbuffer();

    // La dimensione del back buffer deve seguire la finestra, o la swap chain
    // scala l'immagine e l'interfaccia si vede sfocata.
    const HRESULT resize = swapChain_->ResizeBuffers(0, static_cast<UINT>(width),
                                                     static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resize))
    {
        if (error != nullptr) *error = "Direct3D 11 resize failed";
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || backBuffer == nullptr)
    {
        if (error != nullptr) *error = "Direct3D 11 back buffer unavailable";
        return false;
    }
    const HRESULT view = device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
    backBuffer->Release();
    if (FAILED(view))
    {
        if (error != nullptr) *error = "Direct3D 11 render target creation failed";
        return false;
    }
    return true;
}

void D3D11Renderer::BeginFrame(int width, int height)
{
    if (context_ == nullptr || swapChain_ == nullptr)
        return;

    if (width != viewportWidth_ || height != viewportHeight_ || renderTarget_ == nullptr)
    {
        std::string error;
        if (!EnsureBackbuffer(width, height, &error))
            return;
    }

    viewportWidth_  = width;
    viewportHeight_ = height;
    drawCalls_      = 0;
    viewport_ = D3D11_VIEWPORT{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                               0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport_);

    // Nessun ritaglio all'inizio del fotogramma: il rettangolo intero.
    ClearClip();

    ID3D11RenderTargetView* target = renderTarget_;
    context_->OMSetRenderTargets(1, &target, nullptr);

    const float clear[4] = {0.106f, 0.110f, 0.122f, 1.0f};
    context_->ClearRenderTargetView(renderTarget_, clear);
}

void D3D11Renderer::Present()
{
    if (swapChain_ != nullptr)
        swapChain_->Present(1, 0);
}

void D3D11Renderer::Begin(int width, int height)
{
    viewportWidth_  = width;
    viewportHeight_ = height;
    vertices_.clear();
}

size_t D3D11Renderer::QuadCount() const
{
    return vertices_.size() / (kFloatsPerVertex * kVerticesPerQuad);
}

void D3D11Renderer::ReserveQuads(size_t quads)
{
    vertices_.reserve(quads * kVerticesPerQuad * kFloatsPerVertex);
}

void D3D11Renderer::Draw(const UIDraw& primitive)
{
    if (primitive.rect.w <= 0.0f || primitive.rect.h <= 0.0f)
        return;
    AppendQuad(primitive);
}

void D3D11Renderer::FillRect(Rect rect, Color color, float radius)
{
    Draw(UIDraw::Fill(rect, color, radius));
}

void D3D11Renderer::BorderRect(Rect rect, Color color, float width, float radius)
{
    Draw(UIDraw::Border(rect, color, width, radius));
}

void D3D11Renderer::SetCornerScale(float scale)
{
    cornerScale_ = scale;
}

void D3D11Renderer::SetOpacity(float opacity)
{
    opacity_ = opacity;
}

void D3D11Renderer::AppendQuad(const UIDraw& p)
{
    const Rect  r = p.rect;

    // Vedi `UIRenderer::AppendQuad`: stesse regole, stesso formato di vertice.
    const bool radiusIsStroke = p.plus || p.cross || p.arrow || p.arrowUp || p.arrowLeft ||
                                p.refresh || p.star || p.pin;
    const float cornerScale = radiusIsStroke ? 1.0f : cornerScale_;
    const float alpha       = p.color.a * opacity_;
    const float flags = (p.textured ? 1.0f : 0.0f) + (p.mono ? 2.0f : 0.0f) +
                        (p.triangle ? 4.0f : 0.0f) + (p.triangleDown ? 8.0f : 0.0f) +
                        (p.plus ? 16.0f : 0.0f) + (p.cross ? 32.0f : 0.0f) +
                        (p.arrow ? 64.0f : 0.0f) + (p.arrowUp ? 128.0f : 0.0f) +
                        (p.arrowLeft ? 256.0f : 0.0f) + (p.refresh ? 512.0f : 0.0f) +
                        (p.star ? 1024.0f : 0.0f) + (p.pin ? 2048.0f : 0.0f);

    for (size_t vertex = 0; vertex < kVerticesPerQuad; ++vertex)
    {
        float v[kFloatsPerVertex];

        v[0] = r.x;          v[1] = r.bottom();
        v[2] = r.right();    v[3] = r.bottom();
        v[4] = r.x;          v[5] = r.y;
        v[6] = r.right();    v[7] = r.y;

        v[8]  = p.u0;  v[9]  = p.v0;
        v[10] = p.u1;  v[11] = p.v1;
        v[12] = p.layer;

        v[13] = p.color.r; v[14] = p.color.g; v[15] = p.color.b; v[16] = alpha;

        v[17] = p.shadeLeft; v[18] = p.shadeRight; v[19] = p.shadeBottom; v[20] = p.shadeTop;

        v[21] = p.radius.tl * cornerScale; v[22] = p.radius.tr * cornerScale;
        v[23] = p.radius.br * cornerScale; v[24] = p.radius.bl * cornerScale;

        v[25] = p.strokeWidth;
        v[26] = p.softness;
        v[27] = p.dashOn;
        v[28] = p.dashOff;
        v[29] = flags;

        v[30] = clip_.x;      v[31] = clip_.y;
        v[32] = clip_.right(); v[33] = clip_.bottom();

        vertices_.insert(vertices_.end(), v, v + kFloatsPerVertex);
    }
}

void D3D11Renderer::SetClip(Rect rect)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f)
    {
        ClearClip();
        return;
    }
    clip_ = rect;
}

void D3D11Renderer::ClearClip()
{
    clip_ = Rect{0.0f, 0.0f, static_cast<float>(viewportWidth_),
                 static_cast<float>(viewportHeight_)};
}

void D3D11Renderer::End()
{
    if (vertices_.empty() || context_ == nullptr || renderTarget_ == nullptr)
        return;

    const size_t quadCount = vertices_.size() / (kFloatsPerVertex * kVerticesPerQuad);

    // L'indice e' lo stesso schema dell'OpenGL: quattro vertici per quad, due
    // triangoli che condividono la diagonale. La serie cresce solo per prefisso,
    // quindi il buffer si rifa' solo quando serve piu' grande.
    const size_t wantedIndices = quadCount * kIndicesPerQuad;
    if (indices_.size() < wantedIndices)
    {
        indices_.reserve(wantedIndices * 2);
        while (indices_.size() < wantedIndices)
        {
            const uint32_t base = static_cast<uint32_t>(indices_.size() / kIndicesPerQuad) * 4u;
            indices_.push_back(base + 0);
            indices_.push_back(base + 1);
            indices_.push_back(base + 2);
            indices_.push_back(base + 1);
            indices_.push_back(base + 2);
            indices_.push_back(base + 3);
        }
    }

    const size_t vertexBytes = vertices_.size() * sizeof(float);
    const size_t indexBytes  = indices_.size() * sizeof(uint32_t);

    const auto createBuffer = [&](ID3D11Buffer** buffer, size_t bytes, UINT bind, size_t* capacity) {
        if (*buffer != nullptr && bytes <= *capacity)
            return true;
        if (*buffer != nullptr) { (*buffer)->Release(); *buffer = nullptr; }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(bytes);
        desc.Usage     = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = bind;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, buffer)))
            return false;
        *capacity = bytes;
        return true;
    };

    if (!createBuffer(&vertexBuffer_, vertexBytes, D3D11_BIND_VERTEX_BUFFER, &vertexCapacityBytes_) ||
        !createBuffer(&indexBuffer_, indexBytes, D3D11_BIND_INDEX_BUFFER, &indexCapacityBytes_))
        return;

    const auto upload = [&](ID3D11Buffer* buffer, const void* data, size_t bytes) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, data, bytes);
            context_->Unmap(buffer, 0);
        }
    };
    upload(vertexBuffer_, vertices_.data(), vertexBytes);
    upload(indexBuffer_, indices_.data(), indexBytes);

    // La proiezione, in ordine di riga: `mul(v, M)` in HLSL legge la matrice per
    // righe, quindi e' gia' quella giusta per `uProjection`.
    const float width  = static_cast<float>(viewportWidth_);
    const float height = static_cast<float>(viewportHeight_);
    const float projection[16] = {
        2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / height, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f};
    upload(constantBuffer_, projection, sizeof(projection));

    context_->IASetInputLayout(inputLayout_);
    const UINT stride = static_cast<UINT>(kFloatsPerVertex * sizeof(float));
    const UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
    context_->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R32_UINT, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &constantBuffer_);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetSamplers(0, 1, &sampler_);
    if (textureView_ != nullptr)
        context_->PSSetShaderResources(0, 1, &textureView_);

    if (mipsDirty_ && textureView_ != nullptr)
    {
        context_->GenerateMips(textureView_);
        mipsDirty_ = false;
    }

    const FLOAT blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->OMSetBlendState(blendState_, blendFactor, 0xFFFFFFFF);
    context_->RSSetState(rasterState_);

    ID3D11RenderTargetView* target = renderTarget_;
    context_->OMSetRenderTargets(1, &target, nullptr);

    context_->DrawIndexed(static_cast<UINT>(quadCount * kIndicesPerQuad), 0, 0);
    ++drawCalls_;

    // Un SRV ancora legato impedisce a `GenerateMips` del fotogramma dopo di
    // scriverci sopra.
    ID3D11ShaderResourceView* none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
}
