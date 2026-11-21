# GTX 1050 Ti runs 2026-11-21

## Shader catalog

The latest pre-menu replay is
[`1050ti-pre-menu-v1.1.2-historical-env-061302514`](./1050ti-pre-menu-v1.1.2-historical-env-061302514).
The machine-readable inventory is
[`shader-catalog-v1.json`](./1050ti-pre-menu-v1.1.3-resource-snapshots-185616858/shader-catalog-v1.json).

The catalog distinguishes three classes:

1. **Ready**: hash-matched SM5 override successfully loaded.
2. **Executed**: an already measured VS/PS pair was invoked on the graphics
   pipeline.
3. **Placeholder**: usable SM5 bytecode was missing; it is not assumed to be
   on the target menu RTV path.

The catalog contains 13 ready shaders and three executed graphics pairs. The
complete run log also contains thousands of placeholders, including compute
shader placeholders. They remain candidates for analysis, not automatically
targets for conversion.

The run read back 11 of 100 sampled source/backbuffer points at Present #100,
but 0 of 921,600 pixels at Present #1400. The next work is to correlate the
source/RTV transition and the exact bound shaders/resources at that interval.

## Resource-snapshot diagnostic v1.1.3

The proxy diagnostic build now preserves `BB_SOURCE_HISTORY` after Present
#100 and emits a snapshot at every 100th Present. Each resource keeps up to
128 recent draw, skip, or clear entries. The first versioned package is
[`1050ti-pre-menu-v1.1.3-resource-snapshots`](./profiles/1050ti-pre-menu-v1.1.3-resource-snapshots),
with a run-ready launcher and a pinned runtime/package manifest.

This change is diagnostic-only: it does not alter draw execution or the
backbuffer copy path. Its result must be compared against the source resources
and recent shader pairs at Presents #100, #200, #500, #1000, and #1400.

### v1.1.3 measured result

Run
[`1050ti-pre-menu-v1.1.3-resource-snapshots-185616858`](./1050ti-pre-menu-v1.1.3-resource-snapshots-185616858)
reached Present #1400 with verified runtime modules and restored DLLs.
Three source resources remained registered at every snapshot from Present
#100 through #1400:

| Present | Source/backbuffer sampled non-black |
|---:|---:|
| 100 | 11/100 |
| 200 | 11/100 |
| 300 | 2/100 |
| 400 | 2/100 |
| 500 | 0/100 |
| 1400 | 0/921600 source and backbuffer |

The same three resources had zero skipped draws and zero clears in the
snapshots. The sampled draw ledger contained only three executed graphics
pairs:

- `20e0826b236597a1` / `44b0a992c2304254`
- `20e0826b236597a1` / `d77600bde0076c33`
- `4ce363172cfd70be` / `4c1bc8bf7e457acd`

The target shader diagnostic confirmed complete PS invocations and non-black
1x1 SRVs/white texels for the startup paths, but it does not prove that every
later menu resource or texture binding remains valid. Consequently, the next
step is to add bounded resource/texture/binding snapshots for later executed
pairs, not to convert the 5726 placeholder hashes by count.

## Archived baselines

Do not delete or reset existing 2026-11-12 through 2026-11-19 evidence. Keep
obsolete profiles in evidence as negative/archived baselines; mark them
deprecated in their README or manifest instead of removing them.

## Run and shader version policy

Every Hela run uses a GPU-specific, immutable test version:

- `v1.1.0`: replay baseline with shader dumping enabled.
- `v1.1.1`: replay baseline with dumping disabled and run-local overrides.
- `v1.1.2`: exact historical initialization environment plus NVIDIA masking,
  tiled tier 3, caller tracing off and run-local paths.
- `v1.1.3`: diagnostic-only resource snapshots added after the v1.1.2 baseline;
  it changes no draw or copy behavior.

A shader can advance through the catalog only by measured evidence:

1. **Cataloged**: the hash and stage were observed in a real run.
2. **Ready**: hash-matched SM5 bytecode loaded successfully.
3. **Executed**: the graphics or compute pipeline invoked a PSO containing
   that shader in the recorded run.
4. **Target-correlated**: the shader or resource pair was observed on the
   source RTV/copy-to-backbuffer path and correlated with a readback.
5. **Pixel-validated**: a full source/backbuffer BMP demonstrates non-black,
   stable pixels for a real menu frame.

The working set is limited to ready shaders and executed pairs. The 13 ready
shaders and three executed graphics pairs are not individually promoted; only
the exact pair/hash evidence is reused.

A placeholder remains unresolved until it is proven to be on the target RTV
or Present copy path. A missing shader outside that path is archived with the
run and not converted as an unrelated broad batch. The number of placeholder
hashes is therefore not a target-completion metric.

Future versions append a suffix for the single new diagnostic or fix:

- `...-resource-snapshots`
- `...-binding-snapshots`
- `...-target-shader`
- `...-menu-pixels`

They must pin runtime DLL SHA-256, override SHA-256, GPU, launch profile,
target Present, and restoration state. Old run directories remain negative or
historical evidence; they are not deleted or silently overwritten.
