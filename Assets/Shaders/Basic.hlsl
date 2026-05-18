// Basic.hlsl — Cook-Torrance PBR (metallic-roughness) + PCF shadow mapping
//
// BRDF: GGX NDF (D) · Smith-Schlick-GGX geometry (G) · Schlick Fresnel (F)
// Lighting model: single directional light, constant ambient, no IBL.
// Shadow: 3×3 PCF from a 2048×2048 orthographic shadow map.

// ---------------------------------------------------------------------------
// Constant buffers
// ---------------------------------------------------------------------------

cbuffer PerObjectCB : register(b0)
{
    float4x4 MVP;       // (World * View * Proj) transposed for GPU upload
    float4x4 World;     // world matrix (transposed)
    float4x4 LightMVP;  // world * lightView * lightOrtho (transposed)
};

cbuffer LightCB : register(b1)
{
    float3 LightDir;    // normalized, pointing TOWARD the light source
    float  Intensity;
    float3 LightColor;
    float  Pad0;
    float3 Ambient;     // constant ambient radiance
    float  Pad1;
    float3 CameraPos;   // world-space camera position (for view direction in PBR)
    float  Pad2;
    float2 ScreenSize;  // viewport size in pixels (for SSAO screen-UV)
    float2 ScreenPad;
};

cbuffer MaterialCB : register(b2)
{
    float4 MatAlbedo;             // base color (RGBA)
    float  MatRoughness;          // perceptual roughness [0,1]
    float  MatMetallic;           // metallic factor [0,1]
    float  MatEmissiveIntensity;  // emissive multiplier
    float  MatPad0;
    float3 MatEmissive;           // emissive color
    float  MatPad1;
};

// ---------------------------------------------------------------------------
// Textures & samplers
// ---------------------------------------------------------------------------

Texture2D              gTexture       : register(t0);  // albedo / diffuse
Texture2D              gShadowMap     : register(t1);  // R32_FLOAT depth map
TextureCube            gIrradianceMap : register(t2);  // IBL diffuse irradiance
TextureCube            gSpecularMap   : register(t3);  // IBL prefiltered specular (mip levels)
Texture2D              gBRDFLUT       : register(t4);  // split-sum BRDF lookup table
Texture2D              gNormalMap     : register(t5);  // tangent-space normal map
Texture2D              gAOMap         : register(t6);  // SSAO ambient-occlusion (R8_UNORM)
SamplerState           gSampler       : register(s0);  // linear wrap (albedo + normal map)
SamplerComparisonState gShadowSampler : register(s1);  // LESS_EQUAL, border-white (PCF)
SamplerState           gEnvSampler    : register(s2);  // linear clamp (IBL cube + LUT)

// ---------------------------------------------------------------------------
// Vertex shader I/O
// ---------------------------------------------------------------------------

struct VSInput
{
    float3 Position  : POSITION;
    float4 Color     : COLOR;
    float2 TexCoord  : TEXCOORD;
    float3 Normal    : NORMAL;
    float3 Tangent   : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct PSInput
{
    float4 Position      : SV_POSITION;
    float4 Color         : COLOR;
    float2 TexCoord      : TEXCOORD0;
    float4 ShadowPos     : TEXCOORD1;  // light clip-space position (for PCF)
    float3 WorldPos      : TEXCOORD2;  // world-space position
    float3 WorldNormal   : TEXCOORD3;
    float3 WorldTangent  : TEXCOORD4;
    float3 WorldBitangent: TEXCOORD5;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.Position      = mul(float4(input.Position, 1.0f), MVP);
    o.Color         = input.Color;
    o.TexCoord      = input.TexCoord;

    float3x3 W3     = (float3x3)World;
    o.WorldNormal    = mul(input.Normal,    W3);
    o.WorldTangent   = mul(input.Tangent,   W3);
    o.WorldBitangent = mul(input.Bitangent, W3);

    float4 wp  = mul(float4(input.Position, 1.0f), World);
    o.WorldPos = wp.xyz;

    o.ShadowPos = mul(float4(input.Position, 1.0f), LightMVP);
    return o;
}

// Number of specular mip levels — must match IBLMap::kSpecMips
static const float IBL_SPEC_MIPS = 6.0f;

// ---------------------------------------------------------------------------
// PCF shadow  — 3×3 kernel, returns 1.0 = lit, 0.0 = shadow
// ---------------------------------------------------------------------------

static const float SHADOW_MAP_TEXEL = 1.0f / 2048.0f;
static const float SHADOW_BIAS      = 0.002f;

float PCFShadow(float4 shadowPos)
{
    float3 proj = shadowPos.xyz / shadowPos.w;

    float2 uv = float2( proj.x * 0.5f + 0.5f,
                       -proj.y * 0.5f + 0.5f);

    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
        return 1.0f;

    float cmp    = proj.z - SHADOW_BIAS;
    float shadow = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2((float)x, (float)y) * SHADOW_MAP_TEXEL;
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, uv + offset, cmp);
        }
    }
    return shadow / 9.0f;
}

// ---------------------------------------------------------------------------
// Cook-Torrance PBR helpers
// NOTE: Tone mapping and gamma correction are applied by BloomPass::PSComposite
//       (Bloom.hlsl) after the scene is captured into the HDR render target.
//       Do NOT add them here — they would double-apply when bloom is active.
// ---------------------------------------------------------------------------

static const float PI = 3.14159265358979f;

// GGX / Trowbridge-Reitz Normal Distribution Function
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Schlick-GGX single-term geometry (used twice in Smith)
float G_SchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / (NdotX * (1.0f - k) + k);
}

// Smith masking-shadowing
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Schlick Fresnel
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// ---------------------------------------------------------------------------
// Pixel shader
// ---------------------------------------------------------------------------

float4 PSMain(PSInput input) : SV_TARGET
{
    // --- TBN normal mapping --------------------------------------------------
    // Sample tangent-space normal from normal map, remap [0,1] -> [-1,1]
    float3 tangentNormal = gNormalMap.Sample(gSampler, input.TexCoord).rgb * 2.0f - 1.0f;

    // Gram-Schmidt re-orthogonalization to keep TBN orthonormal after interpolation
    float3 N_geo = normalize(input.WorldNormal);
    float3 T     = normalize(input.WorldTangent - dot(input.WorldTangent, N_geo) * N_geo);
    float3 B     = cross(N_geo, T);

    // Build TBN matrix (row-major: mul(v, M) convention)
    float3x3 TBN = float3x3(T, B, N_geo);

    // Transform tangent-space normal to world space
    float3 N = normalize(mul(tangentNormal, TBN));

    // --- Geometric quantities ------------------------------------------------
    float3 V = normalize(CameraPos - input.WorldPos);
    float3 L = normalize(LightDir);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 1e-4f);
    float NdotH = max(dot(N, H), 0.0f);
    float HdotV = max(dot(H, V), 0.0f);

    // --- Material values -----------------------------------------------------
    float4 texSample = gTexture.Sample(gSampler, input.TexCoord);
    float3 albedo    = texSample.rgb * input.Color.rgb * MatAlbedo.rgb;
    float  roughness = max(MatRoughness, 0.04f);  // clamp to avoid singularity
    float  metallic  = MatMetallic;

    // F0: dielectric baseline 0.04, full albedo for metals
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // --- Cook-Torrance specular ----------------------------------------------
    float  D        = D_GGX(NdotH, roughness);
    float  G        = G_Smith(NdotV, NdotL, roughness);
    float3 F        = F_Schlick(HdotV, F0);
    float3 specular = (D * G * F) / (4.0f * NdotV * NdotL + 1e-4f);

    // --- Lambertian diffuse --------------------------------------------------
    // Metals have no diffuse (energy absorbed into specular)
    float3 kD      = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
    float3 diffuse = kD * albedo / PI;

    // --- Shadow + direct lighting --------------------------------------------
    float  shadow   = PCFShadow(input.ShadowPos);
    float3 radiance = LightColor * Intensity;
    float3 Lo       = (diffuse + specular) * radiance * NdotL * shadow;

    // --- IBL: diffuse irradiance ---------------------------------------------
    float3 kD_ibl      = (float3(1,1,1) - F) * (1.0f - metallic);
    float3 irradiance  = gIrradianceMap.Sample(gEnvSampler, N).rgb;
    float3 diffuse_ibl = kD_ibl * albedo * irradiance;

    // --- IBL: specular (split-sum approximation) -----------------------------
    float3 R           = reflect(-V, N);
    float  mipLevel    = roughness * (IBL_SPEC_MIPS - 1.0f);
    float3 prefilt     = gSpecularMap.SampleLevel(gEnvSampler, R, mipLevel).rgb;
    float2 envBRDF     = gBRDFLUT.Sample(gEnvSampler, float2(NdotV, roughness)).rg;
    float3 specular_ibl = prefilt * (F0 * envBRDF.x + envBRDF.y);

    float3 ambient_ibl = diffuse_ibl + specular_ibl;

    // --- SSAO ----------------------------------------------------------------
    // Sample the ambient-occlusion map using the pixel's screen-space UV.
    // SV_POSITION.xy gives the pixel centre (e.g. 0.5, 0.5 for the first pixel).
    // ao = 1.0 means fully lit; ao < 1.0 attenuates the IBL ambient term.
    float2 screenUV = input.Position.xy / ScreenSize;
    float  ao       = gAOMap.Sample(gEnvSampler, screenUV).r;

    // --- Emissive ------------------------------------------------------------
    float3 emissive = MatEmissive * MatEmissiveIntensity;

    // Output linear HDR — tone mapping + gamma applied by BloomPass::PSComposite
    float3 color = ambient_ibl * ao + Lo + emissive;
    return float4(color, texSample.a * MatAlbedo.a);
}
