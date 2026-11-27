import { execFile } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

export interface RunResult { code: number; stdout: string; stderr: string; missing?: boolean; }

export function run(cmd: string, args: string[], timeoutMs = 120_000): Promise<RunResult> {
  return new Promise((resolve) => {
    execFile(cmd, args, { timeout: timeoutMs, maxBuffer: 32 * 1024 * 1024, windowsHide: true },
      (err, stdout, stderr) => {
        if (err) {
          const e = err as NodeJS.ErrnoException & { code?: string | number };
          if (e.code === "ENOENT") return resolve({ code: -1, stdout: "", stderr: `Eseguibile non trovato: ${cmd}`, missing: true });
          return resolve({ code: typeof e.code === "number" ? e.code : 1, stdout: String(stdout), stderr: String(stderr) || e.message });
        }
        resolve({ code: 0, stdout: String(stdout), stderr: String(stderr) });
      });
  });
}

/** Cerca fxc.exe: variabile FXC_PATH, poi Windows SDK (versione più recente), poi PATH. */
export function findFxc(): string {
  if (process.env.FXC_PATH) return process.env.FXC_PATH;
  if (process.platform === "win32") {
    for (const root of [process.env["ProgramFiles(x86)"], process.env.ProgramFiles]) {
      if (!root) continue;
      const bin = path.join(root, "Windows Kits", "10", "bin");
      if (!fs.existsSync(bin)) continue;
      const vers = fs.readdirSync(bin).filter((v) => /^10\./.test(v)).sort().reverse();
      for (const v of vers) {
        const p = path.join(bin, v, "x64", "fxc.exe");
        if (fs.existsSync(p)) return p;
      }
    }
  }
  return "fxc";
}
export const dxilSpirvPath = () => process.env.DXIL_SPIRV_PATH || "dxil-spirv";
export const spirvCrossPath = () => process.env.SPIRV_CROSS_PATH || "spirv-cross";
