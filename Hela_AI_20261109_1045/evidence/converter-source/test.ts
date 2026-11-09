import { analyze, formatFindings } from "./analyzer.js";
import { convert } from "./converter.js";
import assert from "node:assert";

const sample = `
// WaveActiveSum in un commento non deve contare
struct Params { float4 tint; float16_t gain; };
ConstantBuffer<Params> g_params : register(b0, space1);
cbuffer Main : register(b0) { float4x4 vp; }
Texture2D<float4> tex : register(t0, space0);
Texture2D<float4> tex2 : register(t0, space2);
SamplerState smp : register(s0);

[shader("pixel")]
float4 main(float2 uv : TEXCOORD0) : SV_Target
{
    uint idx = NonUniformResourceIndex(uv.x > 0.5 ? 1 : 0);
    float16_t2 h = float16_t2(uv);
    float a = select(uv.x > 0.5, select(uv.y > 0.5, 1.0, 2.0), 3.0);
    uint d = dot4add_u8packed(0x01020304, 0x01010101, 0);
    return tex.Sample(smp, uv) * g_params.tint * a + d + h.x;
}
`;
const r = convert(sample);
console.log(r.output); console.log(r.changes); console.log(r.registerMap);
assert(!/space/.test(r.output.replace(/\/\/.*$/gm, "")), "space residuo");
assert(r.output.includes("cbuffer g_params_cb : register(b1)"), "cbuffer atteso su b1");
assert(r.output.includes("register(t1)"), "tex2 -> t1");
assert(r.output.includes("((uv.x > 0.5) ? (((uv.y > 0.5) ? (1.0) : (2.0))) : (3.0))"), "select annidato");
assert(r.output.includes("min16float2 h = min16float2(uv)"), "tipi 16-bit");
assert(!r.output.includes("NonUniformResourceIndex") && !r.output.includes("[shader"), "attributi");
assert.equal(r.remaining.length, 0, "nessun residuo");

const bad = analyze(`float x = WaveActiveSum(1); RaytracingAccelerationStructure s; uint64_t v; t.SampleCmpLevel(a,b,c,d);
[WaveSize(32)] void f(){} // WaveReadLaneAt`);
console.log(formatFindings(bad));
const codes = new Set(bad.map((f) => f.code));
for (const c of ["W001", "R001", "I001", "X003", "A002"]) assert(codes.has(c), c);
assert.equal(bad.filter((f) => f.code === "W001").length, 1, "WaveSize/commenti non devono contare come wave");

// ---- analizzatore DXIL (regressione sul parsing dei binding del disassemblato DXC)
import { analyzeDisasm } from "./dxil-analyzer.js";
import { postprocessHlsl } from "./hlsl-post.js";
const fake = [
  '; Note: shader requires additional functionality:',
  ';       64-bit Atomics on Typed Resources',
  '; Resource Bindings:',
  '; Name                                 Type  Format         Dim      ID      HLSL Bind  Count',
  '; ------------------------------ ---------- ------- ----------- ------- -------------- ------',
  ';                                   cbuffer      NA          NA     CB0            cb0     1',
  ';                                   cbuffer      NA          NA     CB5     cb0,space3     1',
  ';                                   texture  struct         r/o      T0             t0     1',
  ';                                       UAV     u32          2d      U0             u0     1',
  '; ViewId state:',
  '; NumThreads=(64,1,1)',
  '; EntryFunctionName: MainCS',
  '!3 = !{!"cs", i32 6, i32 6}',
  '  %1 = call i64 @dx.op.atomicBinOp.i64(i32 78, %dx.types.Handle %h, i32 7, i32 %a, i32 %b, i32 undef, i64 %c)',
  '  %2 = call i32 @dx.op.quadOp.i32(i32 123, i32 %v, i8 0)',
].join("\n");
const rep = analyzeDisasm(fake);
assert.equal(rep.bindings.length, 4, "binding (incl. cbuffer)");
assert(rep.bindings.some((b) => b.cls === "b" && b.slot === 0 && b.space === 3), "cb0,space3");
assert(rep.findings.some((f) => f.code === "I002") && rep.findings.some((f) => f.code === "W001"), "atomic64 + quad");
assert.equal(rep.verdict, "not-convertible");
assert.equal(rep.numThreads, "(64,1,1)", "numthreads letto dal disassemblato");
assert.equal(rep.entry, "MainCS", "entry originale letto dal disassemblato");

// ---- ripristino metadati compute persi nel passaggio DXIL -> SPIR-V -> HLSL
const computePost = postprocessHlsl(
  "uint _1 = 2u;\nuint _2 = (-_1) & 31u;\nvoid comp_main()\n{\n    return;\n}\n",
  undefined,
  undefined,
  { stage: "cs", numThreads: "(64,1,1)" },
);
assert.equal(computePost.entryPoint, "comp_main", "entry point compute SPIRV-Cross");
assert(computePost.hlsl.includes("[numthreads(64,1,1)]\nvoid comp_main()"), "attributo numthreads ripristinato");
assert(computePost.changes.some((change) => change.toLowerCase().includes("numthreads")), "ripristino riportato tra le modifiche");
assert(computePost.hlsl.includes("(0u - _1) & 31u"), "negazione uint resa esplicita senza cambiare l'aritmetica unsigned");
const invalidComputePost = postprocessHlsl(
  "void comp_main() {}",
  undefined,
  undefined,
  { stage: "cs", numThreads: "(0,1,1)" },
);
assert(!invalidComputePost.hlsl.includes("[numthreads"), "metadati compute invalidi non applicati");
assert(invalidComputePost.warnings.length > 0, "metadati compute invalidi segnalati");
const oversizedComputePost = postprocessHlsl(
  "void comp_main() {}",
  undefined,
  undefined,
  { stage: "cs", numThreads: "(1024,2,1)" },
);
assert(!oversizedComputePost.hlsl.includes("[numthreads"), "gruppi compute oltre i limiti D3D11 non applicati");
const vertexWrapperPost = postprocessHlsl(
  "struct SPIRV_Cross_Input { float4 position : ATTRIBUTE0; };\n" +
  "struct SPIRV_Cross_Output { float4 position : SV_Position; };\n" +
  "void vert_main() {}\n" +
  "SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input) { vert_main(); return (SPIRV_Cross_Output)0; }\n",
  undefined,
  undefined,
  { stage: "vs" },
);
assert.equal(vertexWrapperPost.entryPoint, "main", "lo shader deve essere compilato tramite il wrapper che trasferisce gli input");
const computeWrapperPost = postprocessHlsl(
  "void comp_main() {}\nvoid main() { comp_main(); }\n",
  undefined,
  undefined,
  { stage: "cs", numThreads: "(8,8,1)" },
);
assert.equal(computeWrapperPost.entryPoint, "main", "numthreads va applicato al wrapper compilabile");
assert(computeWrapperPost.hlsl.includes("[numthreads(8,8,1)]\nvoid main()"), "numthreads sul wrapper entry point");
console.log("\nTUTTI I TEST OK");
