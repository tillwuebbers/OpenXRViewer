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
//TextureCube tex : register(t2);
SamplerState texSampler : register(s0);
SamplerState cubemapSampler : register(s1);

#define PI 3.14159265358979323846f

PSVertex MainVS(Vertex input) {
    PSVertex output;
    float4 worldPos = mul(float4(input.Pos, 1), Model);
    output.Pos = mul(worldPos, ViewProjection);
    output.WorldPos = worldPos.xyz;
    output.Color = input.Color;
    output.UV = input.UV;
    return output;
}

float CalcHorizontalPixelSize(float2 uv)
{
    float theta = uv.x * PI;
    float phi = (uv.y * PI / 2.f) - PI / 4.f;

    float circumferenceRatio = abs(cos(phi * 2.f)); // *2 why?

    return min(1.f / circumferenceRatio, 2048); // sourcewidth
}

float4 MainPS(PSVertex input) : SV_TARGET
{
    // Checkerboard Test
    //float checkerboardCount = 8.;
    //float checkerboardOffset = 1. / 16.;
    //float checkerboard = (round((input.UV.x + checkerboardOffset) * checkerboardCount) + round((input.UV.y + checkerboardOffset) * checkerboardCount)) % 2 == 0 ? 0.f : 1.f;
    //return float4(checkerboard, checkerboard, checkerboard, 1);
    //return tex.SampleBias(texSampler, input.UV, 2);

    // Cubemap sample
    //float4x4 _90degRotation = { 0, 0, 1, 0,
    //                          0, 1, 0, 0,
    //                         -1, 0, 0, 0,
    //                          0, 0, 0, 1 };
    //float3 dir = normalize(input.WorldPos - CameraPos);
    //dir = mul(float4(dir, 1), _90degRotation);
    //return tex.Sample(texSampler, dir);

    // Regular texture sample
    return tex.Sample(texSampler, input.UV);
    
    // Adjusted texture sample
    float2 xDerivative = ddx(input.UV);
    float2 yDerivative = ddy(input.UV);
    
    // xDerivative describes the direction and scale of the screen pixel on the input UV
    // as a vector of the screen x direction mapped to the UV coordinates
    // our texture space is actually "stretched" on the texture space x-axis
    
    float latitude = input.UV.y * PI - PI / 2;
    float circumference = cos(latitude);
    float circumferenceRatioToNoDistortion = 1.f / circumference;
    xDerivative.x *= circumferenceRatioToNoDistortion;
    yDerivative.x *= circumferenceRatioToNoDistortion;
    
    return tex.SampleGrad(texSampler, input.UV, xDerivative, yDerivative);
}