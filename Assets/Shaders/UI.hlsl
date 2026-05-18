// =============================================================================
// UI.hlsl — 2D overlay shader for VibeEngine's UI system
//
// Vertex format: float2 pos (screen pixels), float2 uv, float4 color
// Root sig     : b0 = ortho matrix (16 root constants)
//                b1 = useTexture  ( 1 root constant, int)
//                t0 = font/image atlas
//                s0 = linear sampler (static)
// =============================================================================

// --- Constant buffers -------------------------------------------------------
cbuffer MatrixCB : register(b0) { float4x4 g_OrthoProj; }
cbuffer FlagCB   : register(b1) { int g_UseTexture; }

// --- Resources --------------------------------------------------------------
Texture2D    g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

// --- Vertex / pixel structs -------------------------------------------------
struct VSInput {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct PSInput {
    float4 svpos : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

// --- Vertex shader ----------------------------------------------------------
PSInput VS_Main(VSInput v)
{
    PSInput o;
    // Screen-pixel pos → NDC via ortho matrix
    o.svpos = mul(float4(v.pos, 0.0f, 1.0f), g_OrthoProj);
    o.uv    = v.uv;
    o.color = v.color;
    return o;
}

// --- Pixel shader -----------------------------------------------------------
// Mode 0 (g_UseTexture == 0): solid rectangle — color used as-is.
// Mode 1 (g_UseTexture == 1): font/image sampling — alpha multiplied by tex.a.
//   Font atlas stores text as white (RGB=1) with luminance-encoded alpha.
float4 PS_Main(PSInput p) : SV_Target
{
    [branch]
    if (g_UseTexture)
    {
        float4 tex = g_Texture.Sample(g_Sampler, p.uv);
        return float4(p.color.rgb, p.color.a * tex.a);
    }
    return p.color;
}
