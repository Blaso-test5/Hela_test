import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, mkdir, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";
import { runPackageGate } from "./package-gate.mjs";
import { buildRunEvidence, parseLoadedOverrides, bmpNonBlackFraction, validateRunEvidence } from "./run-evidence.mjs";

const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");

function makeBmp(width, height, lit) {
  const rowStride = Math.ceil((width * 3) / 4) * 4;
  const buffer = Buffer.alloc(54 + rowStride * height);
  buffer.write("BM", 0, 2, "ascii");
  buffer.writeUInt32LE(buffer.length, 2);
  buffer.writeUInt32LE(54, 10);
  buffer.writeUInt32LE(40, 14);
  buffer.writeInt32LE(width, 18);
  buffer.writeInt32LE(-height, 22);
  buffer.writeUInt16LE(1, 26);
  buffer.writeUInt16LE(24, 28);
  buffer.writeUInt32LE(0, 30);
  if (lit) {
    buffer[54] = 0x10;
    buffer[55] = 0x80;
    buffer[56] = 0xf0;
  }
  return buffer;
}

async function makeFixture(t) {
  const root = await mkdtemp(path.join(tmpdir(), "hela-evidence-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const packageDir = path.join(root, "package");
  const runDir = path.join(root, "run");
  const outDir = path.join(root, "out");
  await mkdir(path.join(packageDir, "runtime"), { recursive: true });
  await mkdir(path.join(packageDir, "overrides"), { recursive: true });
  await mkdir(path.join(runDir, "readbacks"), { recursive: true });
  const d3d12 = Buffer.from("fake-d3d12");
  const dxgi = Buffer.from("fake-dxgi");
  const cso = Buffer.from("fake-cso");
  await writeFile(path.join(packageDir, "runtime", "d3d12.dll"), d3d12);
  await writeFile(path.join(packageDir, "runtime", "dxgi.dll"), dxgi);
  await writeFile(path.join(packageDir, "overrides", "a.cso"), cso);

  const manifest = {
    schema: "hela-run-manifest/v1",
    runtimeAttestation: "module-and-shader-sha256",
    testVersion: "T1",
    runtime: { d3d12: sha(d3d12), dxgi: sha(dxgi) },
    overrides: { "a.cso": sha(cso) },
    requiredOverrides: ["a.cso"],
  };
  await writeFile(path.join(runDir, "run-manifest.json"), JSON.stringify(manifest));
  await writeFile(path.join(runDir, "manager-run.json"), JSON.stringify({
    schema: "hela-proxy-manager-run/v1",
    run_id: "T1-100",
    test_version: "T1",
  }));
  await writeFile(path.join(runDir, "live-monitor-report.json"), JSON.stringify({
    run_identity: { test_version: "T1", attestation_status: "verified" },
  }));
  await writeFile(path.join(runDir, "d3d12on11.log"),
    `SHADER READY stage=VS hash=1234567890abcdef source=SM5-override path=C:\\Hela\\overrides\\a.cso bytes=${cso.length} sha256=${sha(cso)} model=5_0\n`);
  await writeFile(path.join(runDir, "readbacks", "HelaReadback_Present100_backbuffer.bmp"), makeBmp(8, 4, true));

  await writeFile(path.join(packageDir, "candidate-package.json"), JSON.stringify({
    schemaVersion: 1,
    packageId: "T1",
    runtime: [
      { path: "runtime/d3d12.dll", sha256: sha(d3d12), bytes: d3d12.length },
      { path: "runtime/dxgi.dll", sha256: sha(dxgi), bytes: dxgi.length },
    ],
    overrides: [{ id: "ov-a", path: "overrides/a.cso", sha256: sha(cso), bytes: cso.length }],
  }));
  assert.equal((await runPackageGate(packageDir, { mode: "build" })).ok, true);
  return { packageDir, runDir, outDir };
}

test("loaded override parser and BMP fraction", () => {
  const hash = "ab".repeat(32);
  const log = `SHADER READY stage=PS hash=1234567890abcdef source=SM5-override path=C:\\Hela\\overrides\\a.cso bytes=1 sha256=${hash} model=5_0`;
  assert.equal(parseLoadedOverrides(log).get("a.cso"), hash);
  assert.equal(bmpNonBlackFraction(makeBmp(8, 4, false)), 0);
  assert.equal(bmpNonBlackFraction(makeBmp(8, 4, true)), 1 / 32);
});

test("builds a staged run evidence bundle", async (t) => {
  const fixture = await makeFixture(t);
  const result = await buildRunEvidence({
    runDir: fixture.runDir,
    packageDir: fixture.packageDir,
    outDir: fixture.outDir,
    targetPresent: 100,
  });
  assert.equal(result.report.ok, true, JSON.stringify(result.report.issues));
  assert.equal(result.bundle.runId, "T1-100");
  assert.equal(result.bundle.verdict.menu, "unverified");
  assert.equal(result.bundle.readback.length, 1);
  assert.equal(result.bundle.readback[0].causalChain.status, "candidate-only");
  assert.deepEqual(validateRunEvidence(result.bundle), []);
});

test("legge i JSON di run PowerShell con BOM UTF-8", async (t) => {
  const fixture = await makeFixture(t);
  for (const name of ["run-manifest.json", "manager-run.json", "live-monitor-report.json"]) {
    const file = path.join(fixture.runDir, name);
    const contents = await readFile(file);
    await writeFile(file, Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), contents]));
  }
  const result = await buildRunEvidence({
    runDir: fixture.runDir,
    packageDir: fixture.packageDir,
    outDir: fixture.outDir,
    targetPresent: 100,
  });
  assert.equal(result.report.ok, true, JSON.stringify(result.report.issues));
  assert.equal(result.bundle.runId, "T1-100");
});

test("outDir exists is refused", async (t) => {
  const fixture = await makeFixture(t);
  await mkdir(fixture.outDir, { recursive: true });
  await assert.rejects(buildRunEvidence({
    runDir: fixture.runDir,
    packageDir: fixture.packageDir,
    outDir: fixture.outDir,
  }), /esiste/);
});
