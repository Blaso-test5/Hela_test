import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  convertDxilToHlsl,
  fnv1a64,
  precompileDxilDirectory,
} from "./dxil-precompile.js";

function makeContainer(fourcc: "DXIL" | "SHDR"): Buffer {
  const bytes = Buffer.alloc(52);
  bytes.write("DXBC", 0, "ascii");
  bytes.writeUInt32LE(1, 28);
  bytes.writeUInt32LE(36, 32);
  bytes.write(fourcc, 36, "ascii");
  bytes.writeUInt32LE(8, 40);
  if (fourcc === "DXIL") bytes.writeUInt32LE(0x50, 44);
  return bytes;
}

const fakeDisasm = [
  '; EntryFunctionName: MainPS',
  '!0 = !{!"ps", i32 6, i32 0}',
].join("\n");

export async function runPrecompileTests(): Promise<void> {
  assert.equal(fnv1a64(Buffer.from("hello")), "a430d84680aabd0b", "FNV-1a naming matches the proxy's 64-bit implementation");

  const root = fs.mkdtempSync(path.join(os.tmpdir(), "shader-lab-precompile-test-"));
  try {
    const inputDir = path.join(root, "input");
    const outputDir = path.join(root, "staging");
    fs.mkdirSync(inputDir);
    const inputPath = path.join(inputDir, "pixel.dxil");
    fs.writeFileSync(inputPath, makeContainer("DXIL"));
    fs.writeFileSync(path.join(inputDir, "pixel_dxil.txt"), fakeDisasm);

    let compileCount = 0;
    const progress: Array<{ settled: number; total: number }> = [];
    const report = await precompileDxilDirectory({
      inputDir,
      outputDir,
      onProgress: (item) => progress.push({ settled: item.settled, total: item.total }),
    }, {
      converter: async () => ({
        hlsl: "void frag_main() {}",
        post: { hlsl: "void frag_main() {}", changes: [], registerMap: [], warnings: [], entryPoint: "frag_main" },
        notes: [],
      }),
      fxcPath: () => "fake-fxc",
      runner: async (_command, args) => {
        compileCount++;
        const outputIndex = args.indexOf("/Fo") + 1;
        fs.writeFileSync(args[outputIndex], makeContainer("SHDR"));
        return { code: 0, stdout: "", stderr: "" };
      },
    });

    assert.equal(report.total, 1);
    assert.equal(report.compiled, 1);
    assert.equal(compileCount, 1);
    assert.match(report.results[0].message, /firma con il VS abbinato non verificata/);
    const expectedOutput = path.join(outputDir, `${fnv1a64(fs.readFileSync(inputPath))}.cso`);
    assert.equal(report.results[0].outputPath, expectedOutput);
    assert.equal(fs.readFileSync(expectedOutput).toString("ascii", 0, 4), "DXBC");
    assert.deepEqual(progress[progress.length - 1], { settled: 1, total: 1 });

    const missingDisasmInput = path.join(root, "missing-disasm");
    const missingDisasmOutput = path.join(root, "missing-disasm-staging");
    fs.mkdirSync(missingDisasmInput);
    const missingPath = path.join(missingDisasmInput, "missing.dxil");
    fs.writeFileSync(missingPath, makeContainer("DXIL"));
    let unexpectedConversion = false;
    const missingReport = await precompileDxilDirectory({
      inputDir: missingDisasmInput,
      outputDir: missingDisasmOutput,
    }, {
      converter: async () => {
        unexpectedConversion = true;
        throw new Error("conversion must not run");
      },
    });
    assert.equal(missingReport.blocked, 1, "missing disassembly blocks rather than guessing stage");
    assert.equal(unexpectedConversion, false);
    assert.equal(fs.readdirSync(missingDisasmOutput).length, 0);

    const failedInput = path.join(root, "failed-input");
    const failedOutput = path.join(root, "failed-staging");
    fs.mkdirSync(failedInput);
    const failedPath = path.join(failedInput, "failed.dxil");
    fs.writeFileSync(failedPath, makeContainer("DXIL"));
    fs.writeFileSync(path.join(failedInput, "failed_dxil.txt"), fakeDisasm);
    const failedReport = await precompileDxilDirectory({
      inputDir: failedInput,
      outputDir: failedOutput,
    }, {
      converter: async () => ({
        hlsl: "void frag_main() {}",
        post: { hlsl: "void frag_main() {}", changes: [], registerMap: [], warnings: [], entryPoint: "frag_main" },
        notes: [],
      }),
      fxcPath: () => "fake-fxc",
      runner: async (_command, args) => {
        const outputIndex = args.indexOf("/Fo") + 1;
        fs.writeFileSync(args[outputIndex], makeContainer("SHDR"));
        return { code: 1, stdout: "", stderr: "synthetic FXC error" };
      },
    });
    assert.equal(failedReport.failed, 1);
    assert.match(failedReport.results[0].message, /synthetic FXC error/);
    assert.equal(fs.readdirSync(failedOutput).length, 0, "failed compile leaves no visible or partial .cso");

    const cancelledInput = path.join(root, "cancelled-input");
    const cancelledOutput = path.join(root, "cancelled-staging");
    fs.mkdirSync(cancelledInput);
    fs.writeFileSync(path.join(cancelledInput, "cancelled.dxil"), makeContainer("DXIL"));
    fs.writeFileSync(path.join(cancelledInput, "cancelled_dxil.txt"), fakeDisasm);
    let cancelledConversion = false;
    let cancelAfterSnapshot = false;
    const cancelledReport = await precompileDxilDirectory({
      inputDir: cancelledInput,
      outputDir: cancelledOutput,
      shouldCancel: () => cancelAfterSnapshot,
      onSnapshotProgress: (phase) => {
        if (phase === "hashing") cancelAfterSnapshot = true;
      },
    }, {
      converter: async () => {
        cancelledConversion = true;
        throw new Error("conversion must not run");
      },
    });
    assert.equal(cancelledReport.cancelled, 1);
    assert.equal(cancelledConversion, false);
    assert.equal(fs.readdirSync(cancelledOutput).length, 0);

    const conversionCancelOutput = path.join(root, "conversion-cancel-staging");
    let cancelAfterConversion = false;
    let compileAfterCancellation = false;
    const conversionCancelReport = await precompileDxilDirectory({
      inputDir: cancelledInput,
      outputDir: conversionCancelOutput,
      shouldCancel: () => cancelAfterConversion,
    }, {
      converter: async () => {
        cancelAfterConversion = true;
        return {
          hlsl: "void frag_main() {}",
          post: { hlsl: "void frag_main() {}", changes: [], registerMap: [], warnings: [], entryPoint: "frag_main" },
          notes: [],
        };
      },
      fxcPath: () => "fake-fxc",
      runner: async () => {
        compileAfterCancellation = true;
        return { code: 0, stdout: "", stderr: "" };
      },
    });
    assert.equal(conversionCancelReport.cancelled, 1, "cancellation is observed after conversion completes");
    assert.equal(compileAfterCancellation, false, "FXC is not started after cancellation at the checkpoint");
    assert.equal(fs.readdirSync(conversionCancelOutput).length, 0);

    const snapshotCancelOutput = path.join(root, "snapshot-cancel-staging");
    let cancelDuringSnapshot = false;
    await assert.rejects(() => precompileDxilDirectory({
      inputDir: cancelledInput,
      outputDir: snapshotCancelOutput,
      shouldCancel: () => cancelDuringSnapshot,
      onSnapshotProgress: (phase) => {
        if (phase === "enumerating") cancelDuringSnapshot = true;
      },
    }), /Annullato durante la snapshot/);
    assert.equal(fs.readdirSync(snapshotCancelOutput).length, 0);

    const blockedInput = path.join(root, "blocked.dxil");
    const blockedDisasm = path.join(root, "blocked.txt");
    fs.writeFileSync(blockedInput, makeContainer("DXIL"));
    fs.writeFileSync(blockedDisasm, `${fakeDisasm}\n  %1 = call i32 @dx.op.waveActiveOp.i32(i32 1)`);
    await assert.rejects(() => convertDxilToHlsl({
      inputPath: blockedInput,
      disasmPath: blockedDisasm,
    }), /Conversione NON tentata/);
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}
