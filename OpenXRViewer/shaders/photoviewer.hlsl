struct PSVertex {
    float4 Pos : SV_POSITION;
    float3 Color : COLOR0;
    float2 UV : TEXCOORD0;
};
struct Vertex {
    float3 Pos : POSITION;
    float3 Color : COLOR0;
    float2 UV : TEXCOORD0;
};
cbuffer ModelConstantBuffer : register(b0) {
    float4x4 Model;
};
cbuffer ViewProjectionConstantBuffer : register(b1) {
    float4x4 ViewProjection;
};
Texture2D tex : register(t2);
TextureCube cubemap : register(t3);
SamplerState texSampler : register(s0);
SamplerState cubemapSampler : register(s1);

PSVertex MainVS(Vertex input) {
    PSVertex output;
    output.Pos = mul(mul(float4(input.Pos, 1), Model), ViewProjection);
    output.Color = input.Color;
    output.UV = input.UV;
    return output;
}

float4 MainPS(PSVertex input) : SV_TARGET{
    // Checkerboard Test
    float checkerboardCount = 8.;
    float checkerboardOffset = 1. / 16.;
    float checkerboard = (round((input.UV.x + checkerboardOffset) * checkerboardCount) + round((input.UV.y + checkerboardOffset) * checkerboardCount)) % 2 == 0 ? 0.f : 1.f;
    //return float4(checkerboard, checkerboard, checkerboard, 1);
    //return tex.SampleBias(texSampler, input.UV, 2);

    // Cubemap sample
    return cubemap.Sample(cubemapSampler, input.UV);

    // Regular texture sample
    return tex.Sample(texSampler, input.UV);
}