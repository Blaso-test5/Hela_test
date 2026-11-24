export type Severity = "blocker" | "warning" | "info";

export interface Finding {
  severity: Severity;
  code: string;
  message: string;
  line: number;
  excerpt: string;
  autoFix: boolean;
}

interface Rule {
  code: string;
  severity: Severity;
  re: RegExp;
  message: string;
  autoFix: boolean;
}

/** Sostituisce i commenti con spazi mantenendo righe e colonne. */
export function maskComments(src: string): string {
  return src
    .replace(/\/\*[\s\S]*?\*\//g, (m) => m.replace(/[^\n]/g, " "))
    .replace(/\/\/.*$/gm, (m) => " ".repeat(m.length));
}

const RULES: Rule[] = [
  // ---- BLOCKER: nessun equivalente in SM5 ----
  { code: "W001", severity: "blocker", autoFix: false,
    re: /\b(?:Wave(?!Size\b)[A-Z]\w*|Quad(?:ReadLaneAt|ReadAcross\w+|Any|All))\b/,
    message: "Wave/Quad intrinsics (SM6.0+): nessun equivalente in SM5. Serve riscrivere l'algoritmo (es. groupshared + barrier)." },
  { code: "R001", severity: "blocker", autoFix: false,
    re: /\b(?:RaytracingAccelerationStructure|RayQuery|TraceRay|RayDesc|DispatchRaysIndex|DispatchRaysDimensions|ReportHit|CallShader|IgnoreHit|AcceptHitAndEndSearch)\b/,
    message: "Ray tracing (DXR): impossibile in D3D11. Lo shader va rimosso o sostituito da un fallback raster." },
  { code: "M001", severity: "blocker", autoFix: false,
    re: /\b(?:DispatchMesh|SetMeshOutputCounts|outputtopology)\b/,
    message: "Mesh/Amplification shader: non esistono in D3D11. Servono VS/GS/CS equivalenti." },
  { code: "D001", severity: "blocker", autoFix: false,
    re: /\b(?:ResourceDescriptorHeap|SamplerDescriptorHeap)\b/,
    message: "Dynamic resources (SM6.6, bindless): D3D11 usa binding a slot. Serve un binding esplicito." },
  { code: "D002", severity: "blocker", autoFix: false,
    re: /\b\w+\s+\w+\s*\[\s*\]\s*:\s*register\s*\(/,
    message: "Array di risorse senza dimensione (unbounded): non supportato in SM5.0." },
  { code: "I001", severity: "blocker", autoFix: false,
    re: /\bu?int64_t\w*/,
    message: "Interi a 64 bit (SM6.0+): non supportati in SM5." },
  { code: "I002", severity: "blocker", autoFix: false,
    re: /\bInterlocked\w*(?:64|Float\w*)\b/,
    message: "Atomiche 64-bit / float (SM6.6): non supportate in SM5." },
  { code: "S001", severity: "blocker", autoFix: false,
    re: /\bSV_(?:ViewID|Barycentrics|ShadingRate|CullPrimitive|StartVertexLocation|StartInstanceLocation)\b/,
    message: "Semantica SV_* disponibile solo da SM6.x." },
  { code: "X001", severity: "blocker", autoFix: false,
    re: /\bGetAttributeAtVertex\b/,
    message: "GetAttributeAtVertex (SM6.1): nessun equivalente in SM5." },
  { code: "X002", severity: "blocker", autoFix: false,
    re: /\b(?:pack_(?:clamp_)?[us]8|unpack_[us]8[us](?:16|32))\b/,
    message: "Intrinsics pack/unpack 8-bit (SM6.6): non emulati automaticamente." },
  { code: "X003", severity: "blocker", autoFix: false,
    re: /\.\s*SampleCmp(?:Level|Bias|Grad)\b/,
    message: "SampleCmpLevel/Bias/Grad (SM6.7/6.8): in SM5 esiste solo SampleCmp/SampleCmpLevelZero." },
  { code: "T002", severity: "blocker", autoFix: false,
    re: /\btemplate\s*<|\bconstexpr\b|\boperator\s*[-+*\/=<>!%&|^~\[\]()]+|\benum\s+class\b|\bnamespace\s+\w+/,
    message: "Feature HLSL 2021 (template/operator/enum class/namespace): non supportate da FXC." },
  // ---- WARNING: convertibili automaticamente (con possibili differenze) ----
  { code: "X004", severity: "warning", autoFix: true,
    re: /\bdot4add_[iu]8packed\b/,
    message: "dot4add_*8packed (SM6.4): emulato con funzione helper (più lento)." },
  { code: "X005", severity: "warning", autoFix: true,
    re: /\bdot2add\b/,
    message: "dot2add (SM6.4): emulato con funzione helper." },
  { code: "N001", severity: "warning", autoFix: true,
    re: /\bNonUniformResourceIndex\b/,
    message: "NonUniformResourceIndex: rimosso. Verifica che l'indice sia dinamicamente valido in SM5." },
  { code: "T001", severity: "warning", autoFix: true,
    re: /\b(?:u?int|float)16_t(?=[1-4]?(?:x[1-4])?\b)/,
    message: "Tipi 16-bit nativi: convertiti in min16float/min16int/min16uint (su D3D11 sono solo hint di precisione)." },
  { code: "T003", severity: "warning", autoFix: true,
    re: /(?<![\w.])select\s*\(/,
    message: "select() (HLSL 2021): convertito in operatore ternario." },
  { code: "B002", severity: "warning", autoFix: true,
    re: /\bConstantBuffer\s*</,
    message: "ConstantBuffer<T> (SM5.1): convertito in cbuffer (gli array di ConstantBuffer non sono convertibili)." },
  { code: "A002", severity: "warning", autoFix: true,
    re: /\[\s*WaveSize\s*\(/,
    message: "[WaveSize]: attributo rimosso." },
  // ---- INFO ----
  { code: "B001", severity: "info", autoFix: true,
    re: /register\s*\([^)]*space\d+/,
    message: "register space: rimappato su slot univoci (SM5.0 non ha spazi di registro). Aggiorna il binding lato C++." },
  { code: "A001", severity: "info", autoFix: true,
    re: /\[\s*shader\s*\(|\bvk::/,
    message: "Attributi [shader(...)] / vk:: rimossi." },
  { code: "P001", severity: "info", autoFix: false,
    re: /^\s*#\s*include\b/,
    message: "#include non espanso dal plugin: preprocessa prima (fxc /P oppure dxc -P)." },
];

export function analyze(src: string): Finding[] {
  const masked = maskComments(src).split("\n");
  const orig = src.split("\n");
  const out: Finding[] = [];
  for (const rule of RULES) {
    masked.forEach((line, i) => {
      if (new RegExp(rule.re.source).test(line)) {
        out.push({
          severity: rule.severity, code: rule.code, message: rule.message,
          line: i + 1, excerpt: (orig[i] ?? "").trim().slice(0, 160), autoFix: rule.autoFix,
        });
      }
    });
  }
  return out.sort((a, b) => a.line - b.line || a.code.localeCompare(b.code));
}

export function formatFindings(findings: Finding[]): string {
  if (!findings.length) return "Nessuna incompatibilità SM6→SM5 rilevata.";
  const icon = { blocker: "✖ BLOCCANTE", warning: "⚠ AVVISO", info: "ℹ INFO" } as const;
  const counts = { blocker: 0, warning: 0, info: 0 };
  findings.forEach((f) => counts[f.severity]++);
  const head = `Bloccanti: ${counts.blocker} | Avvisi: ${counts.warning} | Info: ${counts.info}`;
  const body = findings
    .map((f) => `${icon[f.severity]} [${f.code}] riga ${f.line}${f.autoFix ? " (auto-fix)" : ""}\n    ${f.message}\n    > ${f.excerpt}`)
    .join("\n");
  return `${head}\n\n${body}`;
}
