struct PSVertex {
    float4 Pos : SV_POSITION;
    float3 Color : COLOR0;
    float2 UV : TEXCOORD0;
    float3 WorldPos : TEXCOORD1;
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
    float3 CameraPos;
};
Texture2D tex : register(t2);
TextureCube cubemap : register(t3);
SamplerState texSampler : register(s0);
SamplerState cubemapSampler : register(s1);

PSVertex MainVS(Vertex input) {
    PSVertex output;
    float4 worldPos = mul(float4(input.Pos, 1), Model);
    output.Pos = mul(worldPos, ViewProjection);
    output.WorldPos = worldPos.xyz;
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
    float3 dir = normalize(input.WorldPos - CameraPos);
    return cubemap.Sample(cubemapSampler, dir);

    // Regular texture sample
    return tex.Sample(texSampler, input.UV);
    
    // Adjusted texture sample
    
    // xDerivative is a vector in texture space that describes the rate of change of texture pixels to screen pixels
    float2 xDerivative = ddx(input.UV);
    float2 yDerivative = ddy(input.UV);
    
    // our texture space is actually "stretched" on the texture space x-axis, so we will have to edit both values.
    float distanceFromCenter = abs(input.UV.y - 0.5) * 2.;
    xDerivative.x *= distanceFromCenter;
    yDerivative.x *= distanceFromCenter;
    
    return float4(1.f, 1.f, 0.8f, 1.f);
    
    return tex.Sample(texSampler, input.UV);
    
    return tex.SampleGrad(texSampler, input.UV, xDerivative, yDerivative);
}