Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

float4 main(float4 uv : TEXCOORD5) : SV_TARGET
{
    float2 sampleUv = float2(uv.z * uv.x, uv.w * uv.y);
    return float4(texture0.Sample(sampler0, sampleUv).rgb, 1.0);
}
