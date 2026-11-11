cbuffer _13_15 : register(b0)
{
    float4 _15_m0[1] : packoffset(c0);
};

Texture2D<float4> _8 : register(t0);
SamplerState _18 : register(s0);

static float4 COLOR;
static float4 COLOR_1;
static float4 ORIGINAL_POSITION;
static float2 TEXCOORD;
static float4 TEXCOORD_1;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float4 COLOR : TEXCOORD1;
    float4 COLOR_1 : TEXCOORD2;
    float4 ORIGINAL_POSITION : TEXCOORD3;
    float2 TEXCOORD : TEXCOORD4;
    float4 TEXCOORD_1 : TEXCOORD5;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

void frag_main()
{
    float4 _57 = _8.Sample(_18, float2(TEXCOORD_1.z * TEXCOORD_1.x, TEXCOORD_1.w * TEXCOORD_1.y));
    float _62 = _57.x * COLOR.x;
    float _63 = _57.y * COLOR.y;
    float _64 = _57.z * COLOR.z;
    float _147;
    float _148;
    float _149;
    if (_15_m0[0u].w != 1.0f)
    {
        _147 = clamp((_15_m0[0u].w * (_62 + (-0.25f))) + 0.25f, 0.0f, 1.0f);
        _148 = clamp((_15_m0[0u].w * (_63 + (-0.25f))) + 0.25f, 0.0f, 1.0f);
        _149 = clamp((_15_m0[0u].w * (_64 + (-0.25f))) + 0.25f, 0.0f, 1.0f);
    }
    else
    {
        _147 = _62;
        _148 = _63;
        _149 = _64;
    }
    float _142;
    float _143;
    float _144;
    if (_15_m0[0u].y != 1.0f)
    {
        float _96 = log2(_147) * _15_m0[0u].x;
        float _97 = log2(_148) * _15_m0[0u].x;
        float _98 = log2(_149) * _15_m0[0u].x;
        float _99 = exp2(_96);
        float _100 = exp2(_97);
        float _101 = exp2(_98);
        float _146;
        if (_99 < 0.00313066993840038776397705078125f)
        {
            _146 = _99 * 12.9200000762939453125f;
        }
        else
        {
            _146 = (exp2(_96 * 0.4166666567325592041015625f) * 1.05499994754791259765625f) + (-0.054999999701976776123046875f);
        }
        float _145;
        if (_100 < 0.00313066993840038776397705078125f)
        {
            _145 = _100 * 12.9200000762939453125f;
        }
        else
        {
            _145 = (exp2(_97 * 0.4166666567325592041015625f) * 1.05499994754791259765625f) + (-0.054999999701976776123046875f);
        }
        if (_101 < 0.00313066993840038776397705078125f)
        {
            _142 = _146;
            _143 = _145;
            _144 = _101 * 12.9200000762939453125f;
        }
        else
        {
            _142 = _146;
            _143 = _145;
            _144 = (exp2(_98 * 0.4166666567325592041015625f) * 1.05499994754791259765625f) + (-0.054999999701976776123046875f);
        }
    }
    else
    {
        _142 = _147;
        _143 = _148;
        _144 = _149;
    }
    SV_Target.x = _142;
    SV_Target.y = _143;
    SV_Target.z = _144;
    SV_Target.w = (_15_m0[0u].z * ((COLOR.w * (-2.0f)) + 1.0f)) + COLOR.w;
    SV_Target = float4(_62, _63, _64, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    COLOR = stage_input.COLOR;
    COLOR_1 = stage_input.COLOR_1;
    ORIGINAL_POSITION = stage_input.ORIGINAL_POSITION;
    TEXCOORD = stage_input.TEXCOORD;
    TEXCOORD_1 = stage_input.TEXCOORD_1;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
