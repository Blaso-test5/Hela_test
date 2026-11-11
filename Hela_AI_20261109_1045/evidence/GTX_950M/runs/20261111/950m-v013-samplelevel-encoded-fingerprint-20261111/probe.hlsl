 #ifndef PROBE_WRITER_CODE
 #define PROBE_WRITER_CODE 0.375f
 #endif

Texture2D<float4> probeTexture : register(t0);
SamplerState probeSampler : register(s0);

float4 main() : SV_Target0
{
    uint width;
    uint height;
    uint mipLevels;
    probeTexture.GetDimensions(0, width, height, mipLevels);
    const float4 value = probeTexture.SampleLevel(
        probeSampler, float2(0.5f, 0.5f), 0.0f);
    const float dimensionCode =
        width > 0 && height > 0 && mipLevels > 0 ? PROBE_WRITER_CODE : 0.125f;

    return float4(
        0.25f + 0.5f * saturate(value.r),
        0.25f + 0.5f * saturate(value.g),
        dimensionCode,
        1.0f);
}
