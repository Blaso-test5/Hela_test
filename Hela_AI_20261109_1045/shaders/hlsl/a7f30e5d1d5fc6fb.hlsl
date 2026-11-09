static float4 gl_Position;
static int gl_VertexIndex;
static int gl_BaseVertexARB;
struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    uint _15 = uint(gl_VertexIndex) - uint(gl_BaseVertexARB);
    gl_Position.x = float(int(((_15 << 2u) & 4u) + 4294967295u));
    gl_Position.y = float(int(((_15 & 2u) * 4294967294u) | 1u));
    gl_Position.z = 0.5f;
    gl_Position.w = 1.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    gl_BaseVertexARB = 0;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    return stage_output;
}
