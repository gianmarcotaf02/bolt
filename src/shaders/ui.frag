#version 330 core

// ---------------------------------------------------------------------------
// Fragment stage for the UI quad batcher.
//
// A single shader draws every UI primitive:
//
//   filled rounded rectangle .... strokeWidth == 0, softness == 0, no texture
//   hairline / thick border ..... strokeWidth  > 0
//   dashed border ............... strokeWidth  > 0, dashOn + dashOff > 0
//   drop shadow ................. strokeWidth == 0, softness > 0
//   alpha-masked icon or image .. flags bit 0 set, bit 1 for monochrome masks
//   per-edge shading ............ gradient multipliers != 1
//
// The shape is evaluated as a signed distance field, so corners, borders and
// blur stay crisp at any DPI without subdividing geometry or using MSAA on the
// UI layer. This implementation is original work; the technique is standard.
// ---------------------------------------------------------------------------

uniform sampler2DArray uTexture;
uniform float uViewportHeight;

in vec2 vShapeMin;
in vec2 vShapeSize;
flat in vec4  vRadius;
flat in float vStrokeWidth;
flat in float vSoftness;
flat in float vDashOn;
flat in float vDashOff;
flat in float vFlags;

smooth in vec2  vUV;
smooth in float vLayer;
smooth in vec4  vColor;
smooth in vec4  vGradient;

out vec4 outColor;

const float FLAG_TEXTURED = 1.0;
const float FLAG_MONO     = 2.0;
const float FLAG_TRIANGLE = 4.0;
const float FLAG_TRIANGLE_DOWN = 8.0;
const float FLAG_PLUS  = 16.0;
const float FLAG_CROSS = 32.0;

// Distanza da un segmento con le punte tonde: e' la forma di una barra.
float segmentDistance(vec2 point, vec2 a, vec2 b, float radius)
{
    vec2 pa = point - a;
    vec2 ba = b - a;
    float along = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * along) - radius;
}

// Un triangolo che punta a destra, iscritto in un rettangolo centrato
// sull'origine. E' l'intersezione di tre semipiani, quindi il campo e' il massimo
// delle tre distanze: negativa dentro, positiva fuori.
//
// Per farlo puntare in giu' non si scrive una seconda formula: si **ruota il
// punto** di un quarto di giro e si usa la stessa. Due formule per la stessa forma
// divergono al primo ritocco, e qui la seconda sarebbe stata invisibile finche'
// qualcuno non avesse guardato una sezione chiusa.
float triangleDistance(vec2 point, vec2 halfSize, float rounding)
{
    float width  = halfSize.x;
    float height = halfSize.y;

    // I tre vertici: in alto a sinistra, a destra, in basso a sinistra.
    vec2 top    = vec2(-width,  height);
    vec2 tip    = vec2( width,  0.0);
    vec2 bottom = vec2(-width, -height);

    // Distanza dal bordo sinistro: dentro e' "piu' a destra di -width".
    float left = -width - point.x;

    // I due lati obliqui, con la normale rivolta verso l'esterno.
    vec2 upperEdge = normalize(vec2( height, 2.0 * width));
    vec2 lowerEdge = normalize(vec2( height, -2.0 * width));
    float upper = dot(point - top, upperEdge);
    float lower = dot(point - tip, lowerEdge);

    float planes = max(max(left, upper), lower);
    if (rounding <= 0.0)
        return planes;

    // La distanza dal **contorno**, presa come distanza dai tre lati: cosi' gli
    // spigoli si arrotondano invece di restare vivi. Il massimo dei semipiani da'
    // la parte (dentro o fuori), la distanza dal contorno da' quanto.
    float edge = min(min(segmentDistance(point, top, tip, 0.0),
                         segmentDistance(point, tip, bottom, 0.0)),
                     segmentDistance(point, bottom, top, 0.0));
    return (planes < 0.0 ? -edge : edge) - rounding;
}

// Signed distance from a point to the boundary of a rounded box centred on the
// origin. Negative inside, positive outside, zero exactly on the outline.
// Radii are (top-left, top-right, bottom-right, bottom-left).
float roundedBoxDistance(vec2 point, vec2 halfSize, vec4 radius)
{
    float r = point.x > 0.0
        ? (point.y > 0.0 ? radius.z : radius.y)   // right side: bottom-right, top-right
        : (point.y > 0.0 ? radius.w : radius.x);  // left  side: bottom-left,  top-left

    // Never let a radius exceed the smaller half extent, or the field inverts.
    r = clamp(r, 0.0, min(halfSize.x, halfSize.y));

    vec2 offset = abs(point) - halfSize + r;
    return min(max(offset.x, offset.y), 0.0) + length(max(offset, vec2(0.0))) - r;
}


// Il piu': due barre dritte che si incrociano, cioe' la **minima** delle due
// distanze. Lo spessore sta in `vRadius.x` — il campo dei raggi, che per questa
// forma non vuol dire niente — e le punte sono tonde come i bordi di tutto il resto.
float plusDistance(vec2 point, vec2 halfSize, float thickness)
{
    float tip = thickness * 0.5;
    vec2 horizontal = vec2(max(halfSize.x - tip, 0.0), tip);
    vec2 vertical   = vec2(tip, max(halfSize.y - tip, 0.0));
    return min(roundedBoxDistance(point, horizontal, vec4(tip)),
               roundedBoxDistance(point, vertical, vec4(tip)));
}

// La croce: le due diagonali, accorciate quanto basta perche' le punte tonde
// restino dentro il quadrato.
float crossDistance(vec2 point, vec2 halfSize, float thickness)
{
    float tip = thickness * 0.5;
    float extent = max(min(halfSize.x, halfSize.y) - tip, 0.0);
    float backslash = segmentDistance(point, vec2(-extent, -extent), vec2(extent, extent), tip);
    float slash     = segmentDistance(point, vec2(-extent,  extent), vec2(extent, -extent), tip);
    return min(backslash, slash);
}

// Walks the rectangle outline clockwise from the bottom-left corner and reports
// whether the given fragment lands on an "on" segment of the dash pattern.
bool isOnDash(vec2 frag)
{
    float period = vDashOn + vDashOff;
    if (period <= 0.0)
        return true;

    vec2  local = frag - vShapeMin;
    float w = vShapeSize.x;
    float h = vShapeSize.y;
    float along;

    if (local.y <= vStrokeWidth)          along = local.x;                       // bottom edge, left to right
    else if (local.x >= w - vStrokeWidth) along = w + local.y;                   // right edge, bottom to top
    else if (local.y >= h - vStrokeWidth) along = w + h + (w - local.x);         // top edge, right to left
    else                                  along = 2.0 * w + h + (h - local.y);   // left edge, top to bottom

    return mod(along, period) < vDashOn;
}

void main()
{
    // gl_FragCoord originates at the bottom-left with +Y up; UI space is
    // top-left with +Y down, so flip vertically.
    vec2 frag = vec2(gl_FragCoord.x, uViewportHeight - gl_FragCoord.y);

    vec2 halfSize = vShapeSize * 0.5;
    vec2 centre   = vShapeMin + halfSize;
    vec2 point    = centre - frag;

    if ((vDashOn + vDashOff) > 0.0 && !isOnDash(frag))
        discard;

    // --- base colour, optionally modulated by a texture-array layer ---------
    vec4 color = vColor;
    if (mod(vFlags, 2.0 * FLAG_TEXTURED) >= FLAG_TEXTURED)
    {
        vec4 texel = texture(uTexture, vec3(vUV, vLayer));
        if (mod(vFlags, 2.0 * FLAG_MONO) >= FLAG_MONO)
            texel = vec4(texel.r);            // reuse an alpha mask as an icon colour
        color *= texel;
    }

    // --- per-edge shading ---------------------------------------------------
    // Bilinear interpolation of the four edge multipliers across the rectangle.
    vec2  relative   = (frag - vShapeMin) / max(vShapeSize, vec2(1.0));
    float horizontal = mix(vGradient.x, vGradient.y, relative.x);   // left, right
    float vertical   = mix(vGradient.w, vGradient.z, relative.y);   // top, bottom
    color *= horizontal * vertical;

    // --- shape coverage -----------------------------------------------------
    // Inset the box by half the stroke so the field's zero crossing sits on the
    // centre line of the border, then feather by the blur amount.
    float feather = max(vSoftness, 1.0);
    float inset   = vStrokeWidth * 0.5;
    vec4  radius  = max(vRadius, vec4(vStrokeWidth));

    vec2  innerHalf = max(halfSize - vec2(inset + vSoftness), vec2(0.0));
    float distance;
    if (mod(vFlags, 2.0 * FLAG_TRIANGLE) >= FLAG_TRIANGLE)
    {
        // In `point` **entrambi gli assi sono girati** rispetto allo schermo, perche'
        // `point = centro - frag`: +X va a sinistra e +Y va in su'. Il rettangolo
        // arrotondato non se ne accorge — e' simmetrico — ma un triangolo si': senza
        // questa riga la linguetta punta dalla parte sbagliata. Qui il punto torna
        // in uno spazio che si legge: +X a destra, +Y in su'.
        vec2 upright = vec2(-point.x, point.y);

        // Un quarto di giro per la linguetta aperta: la formula resta una sola.
        bool down = mod(vFlags, 2.0 * FLAG_TRIANGLE_DOWN) >= FLAG_TRIANGLE_DOWN;
        vec2 local = down ? vec2(-upright.y, upright.x) : upright;
        distance = triangleDistance(local, innerHalf, vRadius.x);
    }
    else if (mod(vFlags, 2.0 * FLAG_PLUS) >= FLAG_PLUS)
    {
        distance = plusDistance(point, innerHalf, max(vRadius.x, 1.0));
    }
    else if (mod(vFlags, 2.0 * FLAG_CROSS) >= FLAG_CROSS)
    {
        distance = crossDistance(point, innerHalf, max(vRadius.x, 1.0));
    }
    else
    {
        distance = roundedBoxDistance(point, innerHalf, radius);
    }

    float alpha;
    if (vStrokeWidth > 0.0)
    {
        // Border: keep a band of vStrokeWidth centred on the outline.
        alpha = 1.0 - smoothstep(-feather * 0.5, feather * 0.5, abs(distance) - inset);
    }
    else if (vSoftness > 0.0)
    {
        // A large softness turns the fill into a blurred shadow.
        alpha = 1.0 - smoothstep(0.0, max(1.0, vSoftness * 2.0), distance);
    }
    else
    {
        // Crisp fill. Coverage reaches 0 half a pixel outside the boundary and 1
        // half a pixel inside, so the transition is centred on the edge instead of
        // being smeared two pixels outwards. The previous form put a soft halo
        // around every rectangle and every glyph quad, which is what made the
        // whole interface look out of focus.
        alpha = clamp(0.5 - distance, 0.0, 1.0);
    }

    if (alpha <= 0.0)
        discard;

    outColor = vec4(color.rgb, color.a * alpha);
}
