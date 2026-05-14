// Basic.hlsl - Textured geometry with directional lighting

cbuffer PerObjectCB : register(b0)
{
    float4x4 MVP;
    float4x4 World;
};

cbuffer LightCB : register(b1)
{
    float3 LightDir;    // normalized, pointing toward the light source
    float  Intensity;
    float3 LightColor;
    float  Pad0;
    float3 Ambient;
    float  Pad1;
};

Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
    float3 Normal   : NORMAL;
};

struct PSInput
{
    float4 Position    : SV_POSITION;
    float4 Color       : COLOR;
    float2 TexCoord    : TEXCOORD;
    float3 WorldNormal : NORMAL;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.Position    = mul(float4(input.Position, 1.0f), MVP);
    output.Color       = input.Color;
    output.TexCoord    = input.TexCoord;
    output.WorldNormal = mul(input.Normal, (float3x3)World);
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 normal  = normalize(input.WorldNormal);
    float  diffuse = max(dot(normal, normalize(LightDir)), 0.0f);
    float3 light   = Ambient + LightColor * Intensity * diffuse;

    float4 texCol  = gTexture.Sample(gSampler, input.TexCoord) * input.Color;
    return float4(texCol.rgb * light, texCol.a);
}
