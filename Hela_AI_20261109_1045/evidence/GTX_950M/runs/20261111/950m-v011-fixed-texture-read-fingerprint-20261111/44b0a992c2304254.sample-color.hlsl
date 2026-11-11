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
    float _63 = _57.x * COLOR.x;
    float _64 = _57.y * COLOR.y;
    float _65 = _57.z * COLOR.z;
    float _66 = _57.w * COLOR.w;
    float _149;
    float _150;
    float _151;
    if (_15_m0[0u].w != 1.0f)
    {
        _149 = clamp((_15_m0[0u].w * (_63 + (-0.25f))) + 0.25f, 0.0f, 1.0f);
        _150 = clamp((_15_m0[0u].w * (_64 + (-0.25f))) + 0.25f, 0.0f, 1.0f);
        _151 = clamp((_15_m0[0u].w * (_65 + (-0.25f))) + 0.25f, 0.0f, 1.0f);
    }
    else
    {
        _149 = _63;
        _150 = _64;
        _151 = _65;
    }
    float _144;
    float _145;
    float _146;
    if (_15_m0[0u].y != 1.0f)
    {
        float _98 = log2(_149) * _15_m0[0u].x;
        float _99 = log2(_150) * _15_m0[0u].x;
        float _100 = log2(_151) * _15_m0[0u].x;
        float _101 = exp2(_98);
        float _102 = exp2(_99);
        float _103 = exp2(_100);
        float _148;
        if (_101 < 0.00313066993840038776397705078125f)
        {
            _148 = _101 * 12.9200000762939453125f;
        }
        else
        {
            _148 = (exp2(_98 * 0.4166666567325592041015625f) * 1.05499994754791259765625f) + (-0.054999999701976776123046875f);
        }
        float _147;
        if (_102 < 0.00313066993840038776397705078125f)
        {
            _147 = _102 * 12.9200000762939453125f;
        }
        else
        {
            _147 = (exp2(_99 * 0.4166666567325592041015625f) * 1.05499994754791259765625f) + (-0.054999999701976776123046875f);
        }
        if (_103 < 0.00313066993840038776397705078125f)
        {
            _144 = _148;
            _145 = _147;
            _146 = _103 * 12.9200000762939453125f;
        }
        else
        {
            _144 = _148;
            _145 = _147;
            _146 = (exp2(_100 * 0.4166666567325592041015625f) * 1.05499994754791259765625f) + (-0.054999999701976776123046875f);
        }
    }
    else
    {
        _144 = _149;
        _145 = _150;
        _146 = _151;
    }
    SV_Target.x = _144;
    SV_Target.y = _145;
    SV_Target.z = _146;
    SV_Target.w = (_15_m0[0u].z * ((_66 * (-2.0f)) + 1.0f)) + _66;
    SV_Target = _8.Load(int3(0, 0, 0));
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
