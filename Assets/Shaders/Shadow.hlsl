// Shadow.hlsl — Depth-only pass for directional shadow map generation.
//
// Vertex input layout matches the engine's standard Vertex format so that
// existing mesh VBs can be bound without modification.
// No pixel shader: only depth values are written to the shadow map DSV.

cbuffer ShadowPerObjectCB : register(b0)
{
    float4x4 LightMVP;  // world x lightView x lightOrtho (transposed on CPU side)
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color    : COLOR;    // unused, but must match the VB layout
    float2 TexCoord : TEXCOORD; // unused
    float3 Normal   : NORMAL;   // unused
};

float4 VSMain(VSInput input) : SV_POSITION
{
    return mul(float4(input.Position, 1.0f), LightMVP);
}
