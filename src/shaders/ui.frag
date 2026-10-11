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
    float distance  = roundedBoxDistance(point, innerHalf, radius);

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
