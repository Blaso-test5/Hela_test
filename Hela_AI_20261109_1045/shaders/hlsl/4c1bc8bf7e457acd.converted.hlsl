cbuffer _13_15 : register(b0)
{
    float4 _15_m0[7] : packoffset(c0);
};

Texture2D<float4> _8 : register(t0);
SamplerState _18 : register(s0);
SamplerState _19 : register(s1);

static float2 TEXCOORD;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float2 TEXCOORD : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

void frag_main()
{
    float _40 = _15_m0[4u].z * TEXCOORD.x;
    float _41 = _15_m0[4u].w * TEXCOORD.y;
    float4 _47 = _8.Sample(_18, float2(_40, _41));
    float4 _53 = _8.Sample(_19, float2(_40, _41));
    uint4 _60 = asuint(_15_m0[4u]);
    float _91 = ((floor(fmod(float(_60.x) * TEXCOORD.x, 2.0f)) == 0.0f) ? _53.z : _53.x) - _15_m0[0u].w;
    float _92 = _47.y - _15_m0[1u].w;
    float _93 = _47.w - _15_m0[2u].w;
    float _96 = mad(_15_m0[0u].z, _93, mad(_15_m0[0u].y, _92, _91 * _15_m0[0u].x));
    float _99 = mad(_15_m0[1u].z, _93, mad(_15_m0[1u].y, _92, _15_m0[1u].x * _91));
    float _102 = mad(_15_m0[2u].z, _93, mad(_15_m0[2u].y, _92, _15_m0[2u].x * _91));
    float _147;
    float _148;
    float _149;
    if (_60.y == 1u)
    {
        _147 = (_96 > 0.040449999272823333740234375f) ? exp2(log2((abs(_96) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (_96 * 0.077399380505084991455078125f);
        _148 = (_99 > 0.040449999272823333740234375f) ? exp2(log2((abs(_99) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (_99 * 0.077399380505084991455078125f);
        _149 = (_102 > 0.040449999272823333740234375f) ? exp2(log2((abs(_102) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (_102 * 0.077399380505084991455078125f);
    }
    else
    {
        _147 = _96;
        _148 = _99;
        _149 = _102;
    }
    SV_Target.x = _147;
    SV_Target.y = _148;
    SV_Target.z = _149;
    SV_Target.w = 1.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
