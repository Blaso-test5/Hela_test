/**
 * Post-processing dell'HLSL prodotto da spirv-cross partendo da DXIL/SPIR-V.
 * Corregge i problemi osservati sui campioni reali (DXC 1.8 / dxil-spirv / spirv-cross):
 *  1. risorse duplicate sullo stesso registro (stesso tipo)        -> unificate
 *  2. register space perso da spirv-cross (b0 collide con b0,space3) -> slot univoci
 *  3. cbuffer SPIRV_Cross_VertexInfo / gl_BaseInstanceARB (Vulkan)   -> rimossi (SV_InstanceID D3D è già 0-based)
 */
export interface RegisterMapEntry { from: string; to: string }
export interface ShaderMetadata { stage?: string; numThreads?: string }
export interface PostResult { hlsl: string; changes: string[]; registerMap: RegisterMapEntry[]; warnings: string[]; entryPoint?: string }

/** id SPIR-V della variabile -> {set, binding} leggendo le OpDecorate. */
export function parseSpirvBindings(spv: Buffer): Map<number, { set: number; binding: number }> {
  const out = new Map<number, { set?: number; binding?: number }>();
  if (spv.length < 20 || spv.readUInt32LE(0) !== 0x07230203) return new Map();
  for (let i = 20; i + 4 <= spv.length; ) {
    const w = spv.readUInt32LE(i);
    const wc = w >>> 16, op = w & 0xffff;
    if (wc === 0) break;
    if (op === 71 && wc >= 4) {          // OpDecorate target decoration literal
      const target = spv.readUInt32LE(i + 4), dec = spv.readUInt32LE(i + 8), val = spv.readUInt32LE(i + 12);
      if (dec === 33 || dec === 34) {
        const e = out.get(target) ?? {};
        if (dec === 33) e.binding = val; else e.set = val;
        out.set(target, e);
      }
    }
    i += wc * 4;
  }
  const res = new Map<number, { set: number; binding: number }>();
  for (const [id, e] of out) if (e.binding !== undefined) res.set(id, { set: e.set ?? 0, binding: e.binding });
  return res;
}

interface Decl { line: number; kind: "cbuffer" | "res"; type: string; name: string; cls: string; slot: number; id?: number }

const RE_CB = /^cbuffer\s+(\w+)\s*:\s*register\(b(\d+)\)/;
const RE_RES = /^([A-Za-z_][\w<>, ]*?)\s+(\w+)\s*(?:\[\s*\d*\s*\])?\s*:\s*register\(([tsu])(\d+)\)\s*;/;
const esc = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

const SPIRV_CROSS_ENTRY: Record<string, string> = {
  vs: "vert_main", ps: "frag_main", gs: "geom_main",
  hs: "tesc_main", ds: "tese_main", cs: "comp_main",
};

export function postprocessHlsl(
  hlsl: string,
  spv?: Buffer,
  bindingMap?: Record<string, number>,
  metadata: ShaderMetadata = {},
): PostResult {
  const changes: string[] = [], warnings: string[] = [], registerMap: RegisterMapEntry[] = [];
  const lines = hlsl.split("\n");
  const spvInfo = spv ? parseSpirvBindings(spv) : new Map();
  let entryPoint: string | undefined;

  // ---- 1) dichiarazioni con register
  const decls: Decl[] = [];
  lines.forEach((l, i) => {
    let m = l.match(RE_CB);
    if (m) { decls.push({ line: i, kind: "cbuffer", type: "cbuffer", name: m[1], cls: "b", slot: +m[2], id: +(m[1].match(/_(\d+)$/)?.[1] ?? NaN) }); return; }
    m = l.match(RE_RES);
    if (m) decls.push({ line: i, kind: "res", type: m[1].trim(), name: m[2], cls: m[3], slot: +m[4], id: +(m[2].match(/_(\d+)$/)?.[1] ?? NaN) });
  });
  const setOf = (d: Decl) => (d.id !== undefined && !Number.isNaN(d.id) ? spvInfo.get(d.id)?.set ?? 0 : 0);

  // ---- 2) unifica le dichiarazioni identiche (stesso tipo, classe, slot e set)
  const kept = new Map<string, Decl>();
  const removed = new Set<number>();
  const renames: [string, string][] = [];
  for (const d of decls) {
    const key = `${d.cls}${d.slot}|${setOf(d)}`;
    const k = kept.get(key);
    if (!k) { kept.set(key, d); continue; }
    if (k.kind === "res" && d.kind === "res" && k.type === d.type) {
      removed.add(d.line); renames.push([d.name, k.name]);
    } else warnings.push(`Registro ${d.cls}${d.slot} usato da due risorse di tipo diverso (${k.type} / ${d.type}): da risolvere a mano.`);
  }
  let out = lines.filter((_, i) => !removed.has(i)).join("\n");
  for (const [from, to] of renames) out = out.replace(new RegExp(`\\b${esc(from)}\\b`, "g"), to);
  if (renames.length) changes.push(`Unificate ${renames.length} dichiarazioni duplicate sullo stesso registro`);

  const unsignedScalars = new Set([...out.matchAll(/\buint\s+(_\d+)\b/g)].map((match) => match[1]));
  let unsignedNegations = 0;
  for (const name of unsignedScalars) {
    out = out.replace(new RegExp(`\\(\\s*-\\s*${esc(name)}\\s*\\)`, "g"), () => {
      unsignedNegations++;
      return `(0u - ${name})`;
    });
  }
  if (unsignedNegations)
    changes.push(`Espresse ${unsignedNegations} negazioni unsigned come sottrazioni modulo 2^32`);

  // ---- 3) register space -> slot univoci
  const used: Record<string, Set<number>> = { b: new Set(), t: new Set(), s: new Set(), u: new Set() };
  for (const d of kept.values()) if (setOf(d) === 0) used[d.cls].add(d.slot);
  for (const d of kept.values()) {
    const sp = setOf(d);
    if (sp === 0) continue;
    const key = `${d.cls}:${sp}:${d.slot}`;
    let n = bindingMap?.[key];
    if (n === undefined) { n = 0; while (used[d.cls].has(n)) n++; }
    used[d.cls].add(n);
    out = out.replace(new RegExp(`(\\b${esc(d.name)}\\b\\s*:\\s*register\\()${d.cls}${d.slot}(\\))`), `$1${d.cls}${n}$2`);
    registerMap.push({ from: `${d.cls}${d.slot}, space${sp}`, to: `${d.cls}${n}` });
    if (bindingMap?.[key] === undefined)
      warnings.push(`Slot ${d.cls}${n} assegnato localmente a ${d.cls}${d.slot},space${sp}: deve coincidere con il root signature (usa la mappa del wrapper).`);
  }
  if (registerMap.length) changes.push(`Rimappati ${registerMap.length} register con space ≠ 0`);

  // ---- 4) base vertex / base instance di stile Vulkan
  if (/SPIRV_Cross_VertexInfo/.test(out)) {
    out = out.replace(/cbuffer\s+SPIRV_Cross_VertexInfo\s*\{[^}]*\};\s*/g, "")
             .replace(/(gl_BaseInstanceARB|gl_BaseVertexARB)\s*=\s*SPIRV_Cross_Base\w+\s*;/g, "$1 = 0;");
    changes.push("Rimosso cbuffer SPIRV_Cross_VertexInfo (base instance/vertex = 0, come in D3D)");
  }
  if (/\bstatic\s+(?:uint|int|float)\s+_\d+;/.test(out))
    warnings.push("Presenti variabili 'static' non inizializzate generate da valori undef del DXIL: FXC può segnalare avvisi.");

  const generatedEntry = metadata.stage ? SPIRV_CROSS_ENTRY[metadata.stage] : undefined;
  const hasMainEntry = /\b(?:void|[\w<>]+)\s+main\s*\(/.test(out);
  if (hasMainEntry) entryPoint = "main";
  else if (generatedEntry && new RegExp(`\\bvoid\\s+${generatedEntry}\\s*\\(`).test(out))
    entryPoint = generatedEntry;
  if (metadata.stage === "cs" && !metadata.numThreads) {
    warnings.push("NumThreads non presente nel disassemblato DXIL: FXC richiede [numthreads] per il compute shader.");
  } else if (metadata.stage === "cs" && metadata.numThreads) {
    const dimensions = metadata.numThreads.match(/^\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)$/);
    const threadCounts = dimensions?.slice(1).map(Number);
    if (!threadCounts || threadCounts.some((n) => n === 0) ||
        threadCounts[0] > 1024 || threadCounts[1] > 1024 || threadCounts[2] > 64 ||
        threadCounts[0] * threadCounts[1] * threadCounts[2] > 1024) {
      warnings.push(`Metadato NumThreads non valido (${metadata.numThreads}): attributo [numthreads] non aggiunto.`);
    } else if (!entryPoint) {
      warnings.push(`Entry point compute SPIRV-Cross (${generatedEntry}) non trovato: [numthreads] non aggiunto.`);
    } else {
      const attribute = `[numthreads(${threadCounts.join(",")})]`;
      const entryRe = new RegExp(`(\\bvoid\\s+${entryPoint}\\s*\\()`);
      const at = out.search(entryRe);
      const prefix = out.slice(0, at);
      const existing = prefix.match(/\[\s*numthreads\s*\(\s*[\d\s,]+\)\s*\]\s*$/);
      if (existing) {
        if (existing[0].replace(/\s/g, "") !== attribute) {
          warnings.push(`L'attributo [numthreads] esistente non coincide con il DXIL (${metadata.numThreads}): verifica manualmente.`);
        }
      } else {
        out = out.slice(0, at) + `${attribute}\n` + out.slice(at);
        changes.push(`Ripristinato [numthreads]${metadata.numThreads} dal metadato DXIL`);
      }
    }
  }
  return { hlsl: out, changes, registerMap, warnings, entryPoint };
}
