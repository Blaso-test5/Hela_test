cbuffer _10_12 : register(b0)
{
    float4 _12_m0[4] : packoffset(c0);
};


static float4 gl_Position;
static float4 ATTRIBUTE;
static float2 ATTRIBUTE_1;
static float2 ATTRIBUTE_2;
static float4 ATTRIBUTE_3;
static float4 ATTRIBUTE_4;
static uint2 ATTRIBUTE_5;
static float4 COLOR;
static float4 COLOR_1;
static float4 ORIGINAL_POSITION;
static float2 TEXCOORD;
static float4 TEXCOORD_1;

struct SPIRV_Cross_Input
{
    float4 ATTRIBUTE : ATTRIBUTE0;
    float2 ATTRIBUTE_1 : ATTRIBUTE1;
    float2 ATTRIBUTE_2 : ATTRIBUTE2;
    float4 ATTRIBUTE_3 : ATTRIBUTE3;
    float4 ATTRIBUTE_4 : ATTRIBUTE4;
    uint2 ATTRIBUTE_5 : ATTRIBUTE5;
};

struct SPIRV_Cross_Output
{
    float4 COLOR : TEXCOORD1;
    float4 COLOR_1 : TEXCOORD2;
    float4 ORIGINAL_POSITION : TEXCOORD3;
    float2 TEXCOORD : TEXCOORD4;
    float4 TEXCOORD_1 : TEXCOORD5;
    float4 gl_Position : SV_Position;
};

void vert_main(uint vertex_id)
{
    float2 forced_position;
    switch (vertex_id % 4u)
    {
    case 0u: forced_position = float2(-1.0f, 1.0f); break;
    case 1u: forced_position = float2(1.0f, 1.0f); break;
    case 2u: forced_position = float2(-1.0f, -1.0f); break;
    default: forced_position = float2(1.0f, -1.0f); break;
    }
    gl_Position = float4(forced_position, 0.0f, 1.0f); (ATTRIBUTE_3.x > 0.040449999272823333740234375f) ? exp2(log2((abs(ATTRIBUTE_3.x) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (ATTRIBUTE_3.x * 0.077399380505084991455078125f);
    COLOR.y = (ATTRIBUTE_3.y > 0.040449999272823333740234375f) ? exp2(log2((abs(ATTRIBUTE_3.y) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (ATTRIBUTE_3.y * 0.077399380505084991455078125f);
    COLOR.z = (ATTRIBUTE_3.z > 0.040449999272823333740234375f) ? exp2(log2((abs(ATTRIBUTE_3.z) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (ATTRIBUTE_3.z * 0.077399380505084991455078125f);
    COLOR.w = ATTRIBUTE_3.w;
    COLOR_1.x = (ATTRIBUTE_4.x > 0.040449999272823333740234375f) ? exp2(log2((abs(ATTRIBUTE_4.x) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (ATTRIBUTE_4.x * 0.077399380505084991455078125f);
    COLOR_1.y = (ATTRIBUTE_4.y > 0.040449999272823333740234375f) ? exp2(log2((abs(ATTRIBUTE_4.y) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (ATTRIBUTE_4.y * 0.077399380505084991455078125f);
    COLOR_1.z = (ATTRIBUTE_4.z > 0.040449999272823333740234375f) ? exp2(log2((abs(ATTRIBUTE_4.z) * 0.94786727428436279296875f) + 0.0521326996386051177978515625f) * 2.400000095367431640625f) : (ATTRIBUTE_4.z * 0.077399380505084991455078125f);
    COLOR_1.w = ATTRIBUTE_4.w;
    ORIGINAL_POSITION.x = ATTRIBUTE_2.x;
    ORIGINAL_POSITION.y = ATTRIBUTE_2.y;
    ORIGINAL_POSITION.z = 0.0f;
    ORIGINAL_POSITION.w = 1.0f;
    TEXCOORD.x = ATTRIBUTE_1.x;
    TEXCOORD.y = ATTRIBUTE_1.y;
    TEXCOORD_1.x = ATTRIBUTE.x;
    TEXCOORD_1.y = ATTRIBUTE.y;
    TEXCOORD_1.z = ATTRIBUTE.z;
    TEXCOORD_1.w = ATTRIBUTE.w;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input, uint vertex_id : SV_VertexID)
{
    ATTRIBUTE = stage_input.ATTRIBUTE;
    ATTRIBUTE_1 = stage_input.ATTRIBUTE_1;
    ATTRIBUTE_2 = stage_input.ATTRIBUTE_2;
    ATTRIBUTE_3 = stage_input.ATTRIBUTE_3;
    ATTRIBUTE_4 = stage_input.ATTRIBUTE_4;
    ATTRIBUTE_5 = stage_input.ATTRIBUTE_5;
    vert_main(vertex_id);
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.COLOR = COLOR;
    stage_output.COLOR_1 = COLOR_1;
    stage_output.ORIGINAL_POSITION = ORIGINAL_POSITION;
    stage_output.TEXCOORD = TEXCOORD;
    stage_output.TEXCOORD_1 = TEXCOORD_1;
    return stage_output;
}
