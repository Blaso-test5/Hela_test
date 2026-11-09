Texture2D<float4> _8 : register(t0);
Texture2D<float4> _9 : register(t1);

static float4 gl_FragCoord;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

void frag_main()
{
    uint _25 = uint(gl_FragCoord.x);
    uint _26 = uint(gl_FragCoord.y);
    float4 _27 = _8.Load(int3(uint2(_25, _26), 0u));
    float _30 = _27.x;
    float _31 = _27.y;
    float _32 = _27.z;
    float4 _33 = _9.Load(int3(uint2(_25, _26), 0u));
    float _38 = _33.w;
    SV_Target.x = ((_33.x - _30) * _38) + _30;
    SV_Target.y = ((_33.y - _31) * _38) + _31;
    SV_Target.z = ((_33.z - _32) * _38) + _32;
    SV_Target.w = 1.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
