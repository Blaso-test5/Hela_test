float4 main(float4 color : TEXCOORD1) : SV_TARGET
{
    return float4(color.rgb, 1.0);
}
