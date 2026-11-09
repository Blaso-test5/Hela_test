static float4 gl_Position;
static float4 ATTRIBUTE;
static float2 ATTRIBUTE_1;
static float2 TEXCOORD;

struct SPIRV_Cross_Input
{
    float4 ATTRIBUTE : TEXCOORD0;
    float2 ATTRIBUTE_1 : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float2 TEXCOORD : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position.x = ATTRIBUTE.x;
    gl_Position.y = ATTRIBUTE.y;
    gl_Position.z = ATTRIBUTE.z;
    gl_Position.w = ATTRIBUTE.w;
    TEXCOORD.x = ATTRIBUTE_1.x;
    TEXCOORD.y = ATTRIBUTE_1.y;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    ATTRIBUTE = stage_input.ATTRIBUTE;
    ATTRIBUTE_1 = stage_input.ATTRIBUTE_1;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.TEXCOORD = TEXCOORD;
    return stage_output;
}
