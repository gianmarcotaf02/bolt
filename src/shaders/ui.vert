#version 330 core

// ---------------------------------------------------------------------------
// Vertex stage for the UI quad batcher.
//
// Every drawable UI element (panel background, list row, icon, border, shadow)
// is submitted as one axis-aligned quad forming a 4-vertex triangle strip. The
// four corner positions are written identically into all four vertices and
// gl_VertexID selects which corner a given vertex is. The CPU side therefore
// only ever appends one 30-float record four times, which keeps the batching
// code trivially simple and completely stateless.
//
// Design note: the signed-distance-field approach used for antialiased
// corners, borders and shadows is a standard UI rendering technique. This
// implementation is original; see docs/02-architettura-ricostruita.md for the
// study that informed it.
// ---------------------------------------------------------------------------

uniform mat4 uProjection;

layout(location = 0) in vec2 aCornerBL;    // quad corners in UI pixel space,
layout(location = 1) in vec2 aCornerBR;    // origin top-left, +Y pointing down
layout(location = 2) in vec2 aCornerTL;
layout(location = 3) in vec2 aCornerTR;

layout(location = 4) in vec2  aUVMin;      // sub-rectangle of a texture-array layer
layout(location = 5) in vec2  aUVMax;
layout(location = 6) in float aLayer;      // texture-array layer index

layout(location = 7) in vec4  aColor;      // straight (non-premultiplied) rgba
layout(location = 8) in vec4  aGradient;   // per-edge multipliers: left, right, bottom, top
layout(location = 9) in vec4  aRadius;     // corner radii: top-left, top-right, bottom-right, bottom-left
layout(location = 10) in float aStrokeWidth;
layout(location = 11) in float aSoftness;  // blur width in pixels; > 0 turns a fill into a shadow
layout(location = 12) in float aDashOn;
layout(location = 13) in float aDashOff;
layout(location = 14) in float aFlags;     // bit 0 textured, bit 1 monochrome

out vec2 vShapeMin;
out vec2 vShapeSize;
flat out vec4  vRadius;
flat out float vStrokeWidth;
flat out float vSoftness;
flat out float vDashOn;
flat out float vDashOff;
flat out float vFlags;

smooth out vec2  vUV;
smooth out float vLayer;
smooth out vec4  vColor;
smooth out vec4  vGradient;

void main()
{
    // The draw call is indexed, so gl_VertexID is the value fetched from the
    // element buffer, not a 0..3 counter: quad N uses indices 4N..4N+3. Since
    // every quad starts at a multiple of four, the low two bits select the
    // corner within the quad. Using gl_VertexID directly here collapses every
    // quad after the first onto a single corner, producing zero-area geometry.
    //
    // Note: this cannot be declared const. gl_VertexID is a built-in input, not
    // a constant expression, and GLSL requires const initialisers to be constant.
    int corner = gl_VertexID & 3;

    // Corner order: bottom-left, bottom-right, top-left, top-right.
    vec2 position = (corner == 0) ? aCornerBL
                  : (corner == 1) ? aCornerBR
                  : (corner == 2) ? aCornerTL
                                  : aCornerTR;

    // UVs follow the same order, with the source rectangle's top-left at (min.x, min.y).
    vec2 uv = (corner == 0) ? vec2(aUVMin.x, aUVMax.y)
            : (corner == 1) ? aUVMax
            : (corner == 2) ? aUVMin
                            : vec2(aUVMax.x, aUVMin.y);

    gl_Position = uProjection * vec4(position, 0.0, 1.0);

    vShapeMin  = min(min(aCornerBL, aCornerBR), min(aCornerTL, aCornerTR));
    vShapeSize = max(max(aCornerBL, aCornerBR), max(aCornerTL, aCornerTR)) - vShapeMin;

    vRadius      = aRadius;
    vStrokeWidth = aStrokeWidth;
    vSoftness    = aSoftness;
    vDashOn      = aDashOn;
    vDashOff     = aDashOff;
    vFlags       = aFlags;

    vUV       = uv;
    vLayer    = aLayer;
    vColor    = aColor;
    vGradient = aGradient;
}
