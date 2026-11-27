import { analyze, Finding } from "./analyzer.js";

export interface RegisterMapEntry { from: string; to: string; }
export interface ConvertResult {
  output: string;
  changes: string[];
  registerMap: RegisterMapEntry[];
  remaining: Finding[];
}

/** Legge gli argomenti di una chiamata a partire dalla '(' in posizione `open`. */
function parseArgs(src: string, open: number): { args: string[]; end: number } | null {
  let depth = 0, cur = "";
  const args: string[] = [];
  for (let i = open; i < src.length; i++) {
    const c = src[i];
    if ("([{".includes(c)) { if (depth > 0) cur += c; depth++; continue; }
    if (")]}".includes(c)) {
      depth--;
      if (depth === 0) { args.push(cur.trim()); return { args, end: i + 1 }; }
      cur += c; continue;
    }
    if (c === "," && depth === 1) { args.push(cur.trim()); cur = ""; continue; }
    cur += c;
  }
  return null;
}

/** Sostituisce chiamate `nome(...)` con parsing bilanciato delle parentesi (gestisce annidamenti). */
function replaceCalls(src: string, nameRe: RegExp, build: (a: string[]) => string | null) {
  let count = 0, from = 0, s = src;
  const re = new RegExp(nameRe.source, "g");
  for (;;) {
    re.lastIndex = from;
    const m = re.exec(s);
    if (!m) break;
    const open = m.index + m[0].length - 1;
    const call = parseArgs(s, open);
    const rep = call ? build(call.args) : null;
    if (!call || rep === null) { from = m.index + m[0].length; continue; }
    s = s.slice(0, m.index) + rep + s.slice(call.end);
    count++;
    from = m.index;
  }
  return { out: s, count };
}

const HELPER_DOT4_U = `uint dot4add_u8packed(uint a, uint b, uint acc)
{
    return acc + (a & 0xFFu) * (b & 0xFFu) + ((a >> 8) & 0xFFu) * ((b >> 8) & 0xFFu)
               + ((a >> 16) & 0xFFu) * ((b >> 16) & 0xFFu) + (a >> 24) * (b >> 24);
}
`;
const HELPER_DOT4_I = `int _sm5_sx8(uint v, uint s) { return ((int)(v << s)) >> 24; }
int dot4add_i8packed(uint a, uint b, int acc)
{
    return acc + _sm5_sx8(a, 24u) * _sm5_sx8(b, 24u) + _sm5_sx8(a, 16u) * _sm5_sx8(b, 16u)
               + _sm5_sx8(a, 8u) * _sm5_sx8(b, 8u) + _sm5_sx8(a, 0u) * _sm5_sx8(b, 0u);
}
`;
const HELPER_DOT2 = `float dot2add(min16float2 a, min16float2 b, float acc)
{
    return acc + dot((float2)a, (float2)b);
}
`;

export function convert(src: string): ConvertResult {
  const changes: string[] = [];
  const registerMap: RegisterMapEntry[] = [];
  let s = src;

  // 1. Attributi non supportati
  const attrRe = [
    /\[\s*shader\s*\(\s*"[^"]*"\s*\)\s*\]\s*/g,
    /\[\s*WaveSize\s*\([^)]*\)\s*\]\s*/g,
    /\[\[\s*vk::[^\]]*\]\]\s*/g,
  ];
  let attrCount = 0;
  for (const re of attrRe) s = s.replace(re, () => { attrCount++; return ""; });
  if (attrCount) changes.push(`Rimossi ${attrCount} attributi ([shader], [WaveSize], [[vk::...]])`);

  // 2. Register space -> slot univoci
  const regRe = /register\s*\(\s*([bstu])(\d+)\s*(?:,\s*space(\d+)\s*)?\)/g;
  const used: Record<string, Set<number>> = { b: new Set(), s: new Set(), t: new Set(), u: new Set() };
  for (const m of s.matchAll(regRe)) if (!m[3] || m[3] === "0") used[m[1]].add(+m[2]);
  const assigned = new Map<string, number>();
  s = s.replace(regRe, (_m, cls: string, slot: string, space: string | undefined, offset: number, whole: string) => {
    const sp = space ? +space : 0;
    if (sp === 0) return `register(${cls}${slot})`;
    const key = `${sp}:${cls}${slot}`;
    let n = assigned.get(key);
    if (n === undefined) {
      n = +slot;
      if (used[cls].has(n)) { n = 0; while (used[cls].has(n)) n++; }
      used[cls].add(n);
      assigned.set(key, n);
      registerMap.push({ from: `${cls}${slot}, space${sp}`, to: `${cls}${n}` });
      if (/\]\s*:\s*$/.test(whole.slice(Math.max(0, offset - 40), offset)))
        changes.push(`ATTENZIONE: ${cls}${slot},space${sp} è un array di risorse: gli slot successivi potrebbero collidere con altri registri.`);
    }
    return `register(${cls}${n})`;
  });
  if (registerMap.length) changes.push(`Rimappati ${registerMap.length} register con space ≠ 0 (vedi mappa binding)`);
  const limits: Record<string, number> = { b: 14, s: 16, t: 128, u: 64 };
  for (const [cls, set] of Object.entries(used))
    if (set.size && Math.max(...set) >= limits[cls])
      changes.push(`ATTENZIONE: slot '${cls}' oltre il limite D3D11 (${limits[cls]}): max slot usato = ${Math.max(...set)}`);

  // 3. ConstantBuffer<T> -> cbuffer
  let cbCount = 0;
  s = s.replace(/\bConstantBuffer\s*<\s*(\w+)\s*>\s+(\w+)\s*:\s*register\s*\(\s*(b\d+)\s*\)\s*;/g,
    (_m, t: string, n: string, r: string) => { cbCount++; return `cbuffer ${n}_cb : register(${r}) { ${t} ${n}; }`; });
  if (cbCount) changes.push(`Convertiti ${cbCount} ConstantBuffer<T> in cbuffer`);

  // 4. Tipi 16-bit
  let tCount = 0;
  const tr: [RegExp, string][] = [
    [/\bfloat16_t(?=[1-4]?(?:x[1-4])?\b)/g, "min16float"],
    [/\buint16_t(?=[1-4]?(?:x[1-4])?\b)/g, "min16uint"],
    [/\bint16_t(?=[1-4]?(?:x[1-4])?\b)/g, "min16int"],
  ];
  for (const [re, to] of tr) s = s.replace(re, () => { tCount++; return to; });
  if (tCount) changes.push(`Sostituiti ${tCount} tipi 16-bit nativi con min16*`);

  // 5. NonUniformResourceIndex(x) -> (x)
  const nu = replaceCalls(s, /\bNonUniformResourceIndex\s*\(/, (a) => (a.length === 1 ? `(${a[0]})` : null));
  s = nu.out;
  if (nu.count) changes.push(`Rimossi ${nu.count} NonUniformResourceIndex`);

  // 6. select(c,a,b) -> ((c) ? (a) : (b))
  const sel = replaceCalls(
    s,
    /(?<![\w.])(?<!\b(?:float|int|uint|bool|half|double|void)[1-4]?(?:x[1-4])?\s+)select\s*\(/,
    (a) => (a.length === 3 ? `((${a[0]}) ? (${a[1]}) : (${a[2]}))` : null));
  s = sel.out;
  if (sel.count) changes.push(`Convertiti ${sel.count} select() in operatore ternario`);

  // 7. Helper di emulazione
  let helpers = "";
  if (/\bdot4add_u8packed\s*\(/.test(s)) { helpers += HELPER_DOT4_U; changes.push("Aggiunto helper dot4add_u8packed"); }
  if (/\bdot4add_i8packed\s*\(/.test(s)) { helpers += HELPER_DOT4_I; changes.push("Aggiunto helper dot4add_i8packed"); }
  if (/\bdot2add\s*\(/.test(s)) { helpers += HELPER_DOT2; changes.push("Aggiunto helper dot2add"); }
  if (helpers) s = `// ---- helper di emulazione SM6 -> SM5 (generati) ----\n${helpers}// ---- fine helper ----\n\n${s}`;

  const remaining = analyze(s).filter(
    (f) => f.severity !== "info" && f.code !== "X004" && f.code !== "X005");
  return { output: s, changes, registerMap, remaining };
}
