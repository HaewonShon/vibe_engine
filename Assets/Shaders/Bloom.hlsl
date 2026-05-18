// Bloom.hlsl — multi-pass bloom post-processing
//
// Passes (selected by PSO at runtime):
//   VSFullscreen  — screen-space triangle strip (SV_VertexID, no VB)
//   PSBrightPass  — luminance threshold with soft knee
//   PSBlurH       — 13-tap separable Gaussian (horizontal)
//   PSBlurV       — 13-tap separable Gaussian (vertical)
//   PSComposite   — HDR + bloom additive, ACES tone mapping, sRGB gamma
//
// Root signature (matches BloomPass::CreateRootSignature):
//   [0] Root 32-bit constants b0  — BloomCB (8 values)
//   [1] Descriptor table t0       — source texture
//   [2] Descriptor table t1       — bloom texture (composite only)
//   s0 — LINEAR_CLAMP

// ---------------------------------------------------------------------------
// Root constants (b0) — updated per-pass via SetGraphicsRoot32BitConstants
// ---------------------------------------------------------------------------
cbuffer BloomCB : register(b0)
{
    float Threshold;   // luminance threshold for bright pass
    float Intensity;   // bloom additive strength in composite
    float TexelW;      // 1/textureWidth  (set to blur-target texel size)
    float TexelH;      // 1/textureHeight
    float Exposure;    // pre-tone-map exposure multiplier
    float Pad0;
    float Pad1;
    float Pad2;
};

// ---------------------------------------------------------------------------
// Textures & samplers
// ---------------------------------------------------------------------------
Texture2D    gSrc     : register(t0);   // source (HDR or intermediate)
Texture2D    gBloom   : register(t1);   // bloom result (composite only)
SamplerState gSampler : register(s0);   // LINEAR_CLAMP

// ---------------------------------------------------------------------------
// Vertex shader — fullscreen triangle strip (4 verts, no vertex buffer)
// ---------------------------------------------------------------------------
struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD;
};

PSInput VSFullscreen(uint id : SV_VertexID)
{
    // Generates UVs: (0,0) (1,0) (0,1) (1,1) for a triangle strip
    float2 uv = float2((id & 1u) ? 1.0f : 0.0f,
                       (id >> 1u) ? 1.0f : 0.0f);
    PSInput o;
    o.UV  = uv;
    // NDC: u in [0,1] -> x in [-1,1];  v in [0,1] -> y in [1,-1]
    o.Pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    return o;
}

// ---------------------------------------------------------------------------
// Luminance (Rec. 709)
// ---------------------------------------------------------------------------
float Luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// ---------------------------------------------------------------------------
// PSBrightPass — extract pixels brighter than Threshold.
// Soft knee prevents hard edges at the threshold boundary.
// ---------------------------------------------------------------------------
float4 PSBrightPass(PSInput i) : SV_TARGET
{
    float3 c   = gSrc.Sample(gSampler, i.UV).rgb * Exposure;
    float  lum = Luminance(c);

    // Soft knee: remap luminance smoothly around the threshold
    float knee  = Threshold * 0.5f;
    float lo    = Threshold - knee;
    float remap = saturate((lum - lo) / (2.0f * knee + 1e-5f));

    return float4(c * remap, 1.0f);
}

// ---------------------------------------------------------------------------
// 13-tap Gaussian kernel (sigma ~4), weights sum to ~1
// ---------------------------------------------------------------------------
static const int   KERNEL_HALF    = 6;
static const float KERNEL_WEIGHTS[13] = {
    0.002216f, 0.008764f, 0.026995f, 0.064759f, 0.120985f, 0.176033f, 0.199471f,
    0.176033f, 0.120985f, 0.064759f, 0.026995f, 0.008764f, 0.002216f
};

// ---------------------------------------------------------------------------
// PSBlurH — horizontal Gaussian blur.
// TexelW = 1 / blur-target-width  (set to half-res texel width).
// ---------------------------------------------------------------------------
float4 PSBlurH(PSInput i) : SV_TARGET
{
    float3 col = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int k = -KERNEL_HALF; k <= KERNEL_HALF; ++k)
    {
        float2 uv = i.UV + float2(TexelW * (float)k, 0.0f);
        col += gSrc.Sample(gSampler, uv).rgb * KERNEL_WEIGHTS[k + KERNEL_HALF];
    }
    return float4(col, 1.0f);
}

// ---------------------------------------------------------------------------
// PSBlurV — vertical Gaussian blur.
// TexelH = 1 / blur-target-height.
// ---------------------------------------------------------------------------
float4 PSBlurV(PSInput i) : SV_TARGET
{
    float3 col = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int k = -KERNEL_HALF; k <= KERNEL_HALF; ++k)
    {
        float2 uv = i.UV + float2(0.0f, TexelH * (float)k);
        col += gSrc.Sample(gSampler, uv).rgb * KERNEL_WEIGHTS[k + KERNEL_HALF];
    }
    return float4(col, 1.0f);
}

// ---------------------------------------------------------------------------
// ACES Filmic tone mapping (Hill 2015 approximation)
// Maps HDR [0, +inf) to [0, 1] with a filmic S-curve.
// ---------------------------------------------------------------------------
float3 ACESFilmic(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ---------------------------------------------------------------------------
// sRGB gamma encode (IEC 61966-2-1 piecewise transfer function).
// Converts linear light values to display-ready sRGB.
// ---------------------------------------------------------------------------
float3 LinearToSRGB(float3 col)
{
    float3 lo = col * 12.92f;
    float3 hi = pow(abs(col), float3(1.0f/2.4f, 1.0f/2.4f, 1.0f/2.4f)) * 1.055f - 0.055f;
    return float3(
        col.r < 0.0031308f ? lo.r : hi.r,
        col.g < 0.0031308f ? lo.g : hi.g,
        col.b < 0.0031308f ? lo.b : hi.b);
}

// ---------------------------------------------------------------------------
// PSComposite — add bloom to HDR scene, tone map, gamma correct.
//   gSrc   = HDR scene (full res, R16G16B16A16_FLOAT)
//   gBloom = blurred bright pass (half res, upsampled by LINEAR_CLAMP)
// ---------------------------------------------------------------------------
float4 PSComposite(PSInput i) : SV_TARGET
{
    float3 hdr   = gSrc.Sample(gSampler, i.UV).rgb * Exposure;
    float3 bloom = gBloom.Sample(gSampler, i.UV).rgb * Intensity;

    float3 col   = ACESFilmic(hdr + bloom);
    return float4(LinearToSRGB(col), 1.0f);
}
