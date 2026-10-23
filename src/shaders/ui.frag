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
flat in vec4  vClip;

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

// I cinque pulsanti di navigazione: erano i caratteri `←` `→` `↑` `↻` `★`, e
// quattro pesi diversi in una fila di cinque non sono una fila di pulsanti. Adesso
// sono forme, con lo spessore, la misura e il colore che decidiamo noi, e che non
// cambiano col carattere scelto (`docs/02` §7.49, §7.50, e il §7.58 per questi).
const float FLAG_ARROW       = 64.0;
const float FLAG_ARROW_UP    = 128.0;   // con LEFT forma le quattro direzioni
const float FLAG_ARROW_LEFT  = 256.0;
const float FLAG_REFRESH     = 512.0;
const float FLAG_STAR        = 1024.0;
// La puntina delle cartelle **fissate** (`docs/04`, «La puntina dei fissaggi»): la stella resta il
// segno dei segnalibri nella barra strumenti, ma nella barra laterale una cartella
// fissata si legge come nell'originale, con la puntina in fondo alla riga.
const float FLAG_PIN         = 2048.0;

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

// Un triangolo pieno, vertici in senso antiorario: la parte (dentro o fuori) e' il
// massimo delle tre distanze dai lati. Serve **alla punta** dell'aggiorna, che a
// dodici pixel deve restare una punta e non un grumo: due segmenti li' non si
// leggono, un triangolo si'.
float filledTriangle(vec2 point, vec2 a, vec2 b, vec2 c, float rounding)
{
    vec2 ab = normalize(vec2(b.y - a.y, -(b.x - a.x)));
    vec2 bc = normalize(vec2(c.y - b.y, -(c.x - b.x)));
    vec2 ca = normalize(vec2(a.y - c.y, -(a.x - c.x)));

    float planes = max(max(dot(point - a, ab), dot(point - b, bc)), dot(point - c, ca));
    if (rounding <= 0.0)
        return planes;

    float edge = min(min(segmentDistance(point, a, b, 0.0), segmentDistance(point, b, c, 0.0)),
                     segmentDistance(point, c, a, 0.0));
    return (planes < 0.0 ? -edge : edge) - rounding;
}

// Un quadrilatero convesso, vertici in senso antiorario: stessa idea del
// triangolo, un lato in piu'. Serve alla stella, che e' fatta di cinque aquiloni.
float quadDistance(vec2 point, vec2 a, vec2 b, vec2 c, vec2 d)
{
    vec2 n = normalize(vec2(b.y - a.y, -(b.x - a.x)));
    float m = dot(point - a, n);
    n = normalize(vec2(c.y - b.y, -(c.x - b.x)));
    m = max(m, dot(point - b, n));
    n = normalize(vec2(d.y - c.y, -(d.x - c.x)));
    m = max(m, dot(point - c, n));
    n = normalize(vec2(a.y - d.y, -(a.x - d.x)));
    return max(m, dot(point - d, n));
}

// La freccia: **una linea**, non un segnale stradale. Un'asta e una punta a 45°
// (due segmenti, non un triangolo pieno), tutte allo stesso spessore, con le punte
// tonde come il resto delle forme. Punta a destra; le altre direzioni si ottengono
// ruotando il punto, non scrivendo altre tre formule.
float arrowDistance(vec2 point, vec2 halfSize, float thickness)
{
    float r = min(halfSize.x, halfSize.y);
    float t = max(thickness, 1.0) * 0.5;      // raggio delle punte: meta' spessore

    vec2 tail = vec2(-r + t, 0.0);
    vec2 tip  = vec2(r - t, 0.0);
    float shaft = segmentDistance(point, tail, tip, t);

    // La punta e' larga quanto alta: 45°, la forma piu' sobria che resta leggibile
    // a dodici pixel.
    float arm = r * 0.66;
    float head = min(segmentDistance(point, tip, tip + vec2(-arm,  arm), t),
                     segmentDistance(point, tip, tip + vec2(-arm, -arm), t));

    return min(shaft, head);
}

// L'aggiorna: un anello aperto con una punta tangente dentro il taglio. Il taglio
// sta sull'asse +X, cioe' a destra, e la punta gira in senso orario: e' il verso
// che ha `↻`, e non si legge al contrario.
float refreshDistance(vec2 point, vec2 halfSize, float thickness)
{
    float r = min(halfSize.x, halfSize.y);
    float t = max(thickness, 1.0);
    float band = t * 0.5;
    float radius = r - band;
    float reach = length(point);

    float ring = abs(reach - radius) - band;

    // Il taglio: una distanza angolare, riportata in pixel moltiplicando per il
    // raggio, cosi' il ``max`` con l'anello misura la stessa cosa.
    float gapHalf = 0.70;
    float gap = (gapHalf - abs(atan(point.y, point.x))) * max(reach, 0.001);

    // La punta: un triangolo pieno, la base **radiale** e il vertice nel verso del
    // moto. Due segmenti a questa misura non si distinguono da un anello aperto.
    vec2 radial = vec2(cos(gapHalf), sin(gapHalf));
    vec2 tangent = vec2(sin(gapHalf), -cos(gapHalf));
    vec2 p = radial * radius;
    float head = filledTriangle(point, p + radial * t * 1.9, p - radial * t * 1.9,
                                p + tangent * t * 3.4, t * 0.1);

    return min(max(ring, gap), head);
}

// La stella: cinque punte. Piena, perche' il "non e' nei segnalibri" lo dice il
// **bordo** — la stessa forma disegnata con `strokeWidth`.
//
// Due misure diverse per due domande diverse, e non e' un lusso: **dentro o fuori**
// lo dice il segno degli aquiloni (il massimo dei semipiani e' esatto dentro un
// poligono convesso, quindi il segno e' esatto), mentre **quanto dista dal bordo**
// lo dicono i dieci lati presi come segmenti. Con il solo minimo degli aquiloni il
// contorno della stella vuota disegnava una ragnatela sopra le punte: visto.
float starDistance(vec2 point, vec2 halfSize)
{
    float r = min(halfSize.x, halfSize.y);
    float inner = r * 0.32;
    float side = 0.6283185;          // 36 gradi: meta' dell'angolo fra due punte
    float first = 1.5707963;        // la punta in alto

    float edge = 1e6;
    bool  inside = false;

    // Un giro delle dieci punte: intermedia, punta, intermedia, punta...
    vec2 vertex = vec2(cos(first - side), sin(first - side)) * inner;
    for (int i = 0; i < 5; ++i)
    {
        float tipAngle = first + float(i) * 1.2566371;
        vec2 tip   = vec2(cos(tipAngle), sin(tipAngle)) * r;
        vec2 after = vec2(cos(tipAngle + side), sin(tipAngle + side)) * inner;

        edge = min(edge, segmentDistance(point, vertex, tip, 0.0));
        edge = min(edge, segmentDistance(point, tip, after, 0.0));

        // Il segno: la punta e' un aquilone (centro, prima, punta, dopo).
        vec2 before = vertex;
        if (quadDistance(point, vec2(0.0), after, tip, before) < 0.0)
            inside = true;

        vertex = after;
    }

    return inside ? -edge : edge;
}

// La puntina: il segno delle cartelle **fissate**, al posto della stella nella barra
// laterale. E' la forma del riferimento — testa, piatto e punta — e si legge a undici
// pixel perche' e' fatta di tre pezzi soli.
//
// Disegnata **dritta** e poi girata di 45 gradi: obliqua nel campo di distanza
// avrebbe voluto dire tre formule storte invece di tre dritte, e lo stesso ragionamento
// della linguetta e della freccia — si ruota il **punto**, non la forma.
//
// Il piatto e la testa sono due rettangoli arrotondati e la punta un segmento: la
// distanza e' la **minima** delle tre, che e' come si unisce in un campo di distanza
// (l'unione e' il minimo, l'intersezione il massimo).
float pinDistance(vec2 point, vec2 halfSize)
{
    // Un'unita' sola: il lato minore della scatola. La diagonale e' piu' lunga, ed e'
    // li' che una puntina storta sta comoda — senza normalizzare, a undici pixel
    // sarebbe un grumo.
    float unit = max(min(halfSize.x, halfSize.y), 1.0);
    vec2  q    = point / unit;

    // Un quarto di giro in meno di mezzo: 45 gradi, che e' anche l'angolo del
    // riferimento.
    float c = 0.70710678;
    q = vec2(c * q.x - c * q.y, c * q.x + c * q.y);

    // I tre pezzi, generosi: a undici-tredici pixel la forma va **riempita**, non
    // disegnata in filo — una punta da un pixel e mezzo sparisce nell'antialiasing.
    // La barretta e' la testa vista di taglio: e' quella che rende la forma una
    // puntina e non un sasso, quindi e' la piu' larga delle tre.
    float head   = roundedBoxDistance(q - vec2(0.0,  0.40), vec2(0.34, 0.24), vec4(0.12));
    float flange = roundedBoxDistance(q - vec2(0.0,  0.08), vec2(0.66, 0.11), vec4(0.06));
    float needle = segmentDistance(q, vec2(0.0, 0.08), vec2(0.0, -0.98), 0.085);

    return min(min(head, flange), needle) * unit;
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

    // Il ritaglio arriva dal vertice, non dallo stato del disegno: fuori dal
    // rettangolo questo frammento non esiste, esattamente come faceva `glScissor`
    // attorno a un lotto. La differenza e' che qui non costa una draw call, e il
    // rettangolo e' un numero del quadrilatero, confrontabile a meta' pixel invece
    // che troncato all'intero.
    if (frag.x < vClip.x || frag.x >= vClip.z ||
        frag.y < vClip.y || frag.y >= vClip.w)
        discard;

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
    else if (mod(vFlags, 2.0 * FLAG_ARROW) >= FLAG_ARROW)
    {
        // Come per la linguetta: `point` ha tutt'e due gli assi girati, e una
        // freccia se ne accorge. Qui torna in uno spazio leggibile — +X a destra,
        // +Y in su' — e le tre direzioni si fanno ruotando il punto.
        vec2 upright = vec2(-point.x, point.y);
        bool up   = mod(vFlags, 2.0 * FLAG_ARROW_UP) >= FLAG_ARROW_UP;
        bool left = mod(vFlags, 2.0 * FLAG_ARROW_LEFT) >= FLAG_ARROW_LEFT;
        if (up)
            upright = vec2(upright.y, -upright.x);
        if (left)
            upright = -upright;
        distance = arrowDistance(upright, innerHalf, max(vRadius.x, 1.0));
    }
    else if (mod(vFlags, 2.0 * FLAG_REFRESH) >= FLAG_REFRESH)
    {
        vec2 upright = vec2(-point.x, point.y);
        distance = refreshDistance(upright, innerHalf, max(vRadius.x, 1.0));
    }
    else if (mod(vFlags, 2.0 * FLAG_STAR) >= FLAG_STAR)
    {
        distance = starDistance(point, innerHalf);
    }
    else if (mod(vFlags, 2.0 * FLAG_PIN) >= FLAG_PIN)
    {
        // `point` ha gli assi girati: la puntina non e' simmetrica, quindi qui il
        // punto torna in uno spazio leggibile prima di ruotarlo — come la freccia.
        vec2 upright = vec2(-point.x, point.y);
        distance = pinDistance(upright, innerHalf);
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
