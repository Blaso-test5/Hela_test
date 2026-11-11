Texture2D<float4> probeTexture : register(t0);

float4 main() : SV_Target0
{
    uint width;
    uint height;
    uint mipLevels;
    probeTexture.GetDimensions(0, width, height, mipLevels);
    const float4 value = probeTexture.Load(int3(0, 0, 0));
    const float dimensionCode =
        width > 0 && height > 0 && mipLevels > 0 ? 0.75f : 0.25f;

    return float4(
        0.25f + 0.5f * saturate(value.r),
        0.25f + 0.5f * saturate(value.g),
        dimensionCode,
        1.0f);
}
