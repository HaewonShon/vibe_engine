// SSAO.hlsl — Screen-Space Ambient Occlusion
//
// Pass 1 (PSComputeAO): reconstructs view-space positions from the depth
//   buffer, samples a 16-point hemisphere kernel around the estimated surface
//   normal, and outputs an occlusion factor in [0,1]
//   (0 = fully occluded / dark,  1 = fully lit).
//
// Pass 2 (PSBlur): 5-tap cross filter to smooth the raw AO texture.
//
// Both passes share VSFullscreen (SV_VertexID → full-screen triangle, no VB).
//
// Root signature (set by SSAOPass::CreateRootSignature):
//   [0] 40 root 32-bit constants  (b0) — SSAOCB layout
//   [1] Descriptor table: t0 — depth map (AO pass) | AO-raw (Blur pass)
//   [2] Descriptor table: t1 — 4×4 noise (AO pass only, ignored in Blur)
//   s0  static sampler — linear clamp
//   s1  static sampler — point  wrap  (noise tiling)
// ---------------------------------------------------------------------------

// ---- Root constants (40 DWORDs = 160 bytes) --------------------------------
cbuffer SSAOCB : register(b0)
{
    float4x4 InvProj;    // inverse projection (pre-transposed for GPU upload)
    float4x4 Proj;       // projection        (pre-transposed for GPU upload)
    float2   NoiseScale; // AO pass: screen/4 (noise tile);  Blur: texel size
    float    Radius;     // world-space hemisphere radius
    float    Bias;       // depth bias (prevents self-occlusion)
    float    Intensity;  // AO strength multiplier
    float    Pad0, Pad1, Pad2;
};

Texture2D    gTex0     : register(t0); // depth map (AO) | AO-raw (Blur)
Texture2D    gTex1     : register(t1); // 4x4 random noise (AO pass only)
SamplerState gLinClamp : register(s0); // linear clamp
SamplerState gPtWrap   : register(s1); // point  wrap (noise tiling)

// ---------------------------------------------------------------------------
// Full-screen triangle — no vertex buffer required.
// vID 0→(-1,-1) 1→(-1,3) 2→(3,-1) covers the entire NDC clip square.
// ---------------------------------------------------------------------------
struct VSOut {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

VSOut VSFullscreen(uint vID : SV_VertexID)
{
    float2 pos = float2(
        (vID & 1) ? 3.0f : -1.0f,
        (vID & 2) ? -3.0f : 1.0f);
    VSOut o;
    o.Position = float4(pos, 0.0f, 1.0f);
    // DX12: NDC y=+1 → screen top → UV y=0
    o.TexCoord = float2(pos.x * 0.5f + 0.5f,
                        1.0f - (pos.y * 0.5f + 0.5f));
    return o;
}

// ---------------------------------------------------------------------------
// Reconstruct view-space position from screen UV and NDC depth.
// DX12 LH: NDC y=+1 at top, y=-1 at bottom, depth ∈ [0,1].
// ---------------------------------------------------------------------------
float3 ReconstructVS(float2 uv, float depth)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f,
                        1.0f - uv.y * 2.0f,
                        depth, 1.0f);
    float4 vs = mul(ndc, InvProj);   // row-vector convention: v * M
    return vs.xyz / vs.w;
}

// ---------------------------------------------------------------------------
// 16-sample hemisphere kernel (z > 0 → samples face along the +N axis).
// Kernel values are taken from the learnopengl.com SSAO tutorial with minor
// tweaks so all z components are strictly positive (true hemisphere).
// ---------------------------------------------------------------------------
static const float3 KERNEL[16] =
{
    float3( 0.5381f,  0.1856f,  0.4319f),
    float3( 0.1379f,  0.2486f,  0.4430f),
    float3( 0.3371f,  0.5679f,  0.0570f),
    float3(-0.6999f, -0.0451f,  0.0619f),
    float3( 0.0689f, -0.1598f,  0.8547f),
    float3( 0.0560f,  0.0069f,  0.1843f),
    float3(-0.0146f,  0.1402f,  0.0762f),
    float3( 0.0100f, -0.1924f,  0.0344f),
    float3(-0.3577f, -0.5301f,  0.4358f),
    float3(-0.3169f,  0.1063f,  0.0158f),
    float3( 0.0103f, -0.5869f,  0.0846f),
    float3(-0.0897f, -0.4940f,  0.3287f),
    float3( 0.7119f, -0.0154f,  0.0918f),
    float3(-0.0533f,  0.0596f,  0.5411f),
    float3( 0.0352f, -0.0631f,  0.5460f),
    float3(-0.4776f,  0.2847f,  0.0271f),
};

// ---------------------------------------------------------------------------
// AO pass
// ---------------------------------------------------------------------------
float PSComputeAO(VSOut input) : SV_TARGET
{
    float2 uv    = input.TexCoord;
    float  depth = gTex0.SampleLevel(gLinClamp, uv, 0).r;

    // Sky / empty space → fully lit, no AO
    if (depth >= 0.9999f) return 1.0f;

    float3 fragPos = ReconstructVS(uv, depth);

    // ---- Estimate view-space normal from depth derivatives ------------------
    float3 dPdx = ddx(fragPos);
    float3 dPdy = ddy(fragPos);
    float3 N    = normalize(cross(dPdy, dPdx));
    // DX12 LH: camera looks down +Z; the surface normal must face -Z
    // (toward camera) for front-facing geometry.  Flip if wrong direction.
    if (N.z > 0.0f) N = -N;

    // ---- Random rotation via the 4×4 noise texture (tiled over screen) -----
    float2 noiseVec = gTex1.SampleLevel(gPtWrap, uv * NoiseScale, 0).rg
                    * 2.0f - 1.0f;
    float3 rnd      = normalize(float3(noiseVec, 0.0f));

    // Gram-Schmidt: build a tangent perpendicular to N, then bitangent
    float3 tangent   = normalize(rnd - N * dot(rnd, N));
    float3 bitangent = cross(N, tangent);
    // TBN maps from kernel-space (+Z=N) to view space
    float3x3 TBN     = float3x3(tangent, bitangent, N);

    // ---- Hemisphere sampling ------------------------------------------------
    float occlusion = 0.0f;

    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        // Accelerating distribution: nearby samples are weighted by the square
        // of their normalised index, giving denser coverage close to the fragment.
        float scale = (float)i / 16.0f;
        scale = lerp(0.1f, 1.0f, scale * scale);

        // Rotate kernel into view space, apply radius and offset from fragment
        float3 sampleVS = mul(KERNEL[i] * scale, TBN) * Radius + fragPos;

        // Project sample to screen UV
        float4 offset = mul(float4(sampleVS, 1.0f), Proj);
        offset.xyz   /= offset.w;
        float2 sampleUV = float2( offset.x * 0.5f + 0.5f,
                                  1.0f - (offset.y * 0.5f + 0.5f));

        // Read actual scene depth at the projected position
        float  sampleDepth = gTex0.SampleLevel(gLinClamp, sampleUV, 0).r;
        float3 scenePos    = ReconstructVS(sampleUV, sampleDepth);

        // Range check: attenuate contributions from geometry much farther away
        // than our AO radius (avoids false occlusion through door-frames etc.)
        float rangeCheck = smoothstep(0.0f, 1.0f,
            Radius / max(abs(fragPos.z - scenePos.z), 1e-4f));

        // DX12 LH view space: larger Z = further from camera.
        // If the actual scene at the sample UV is closer to camera than the
        // projected sample point, that geometry occludes ambient light.
        occlusion += (scenePos.z < sampleVS.z - Bias ? 1.0f : 0.0f) * rangeCheck;
    }

    return saturate(1.0f - (occlusion / 16.0f) * Intensity);
}

// ---------------------------------------------------------------------------
// Blur pass — 5-tap cross filter (centre + 4 cardinal neighbours)
// NoiseScale is repurposed as texelSize (float2(1/w, 1/h)) for this pass.
// ---------------------------------------------------------------------------
float PSBlur(VSOut input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 tx = NoiseScale;   // = texel size when called from blur pass

    float sum = gTex0.SampleLevel(gLinClamp, uv,                         0).r;
    sum      += gTex0.SampleLevel(gLinClamp, uv + float2( tx.x,  0.0f), 0).r;
    sum      += gTex0.SampleLevel(gLinClamp, uv + float2(-tx.x,  0.0f), 0).r;
    sum      += gTex0.SampleLevel(gLinClamp, uv + float2( 0.0f,  tx.y), 0).r;
    sum      += gTex0.SampleLevel(gLinClamp, uv + float2( 0.0f, -tx.y), 0).r;
    return sum * 0.2f;
}
