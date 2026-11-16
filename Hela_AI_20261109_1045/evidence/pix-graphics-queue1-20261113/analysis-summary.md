# PIX Graphics Queue 1 shader analysis — 2026-11-13

## Collected results

- The PIX event-list export contains 9,588 event rows.
- It references `SetPipelineState` 672 times and 460 distinct PSO IDs.
- The shader map contains 608 shader-stage payload records and 509 unique DXIL hashes:
  - CS: 308 records
  - VS: 152 records
  - PS: 148 records
- All 460 PSO IDs referenced by the event list were reconciled with the PIX C++ export and its 608 shader payloads.
- DXC disassembly completed for all 509 unique DXIL containers.
- Static analysis reported Wave/Quad operations in 167 shaders, 64-bit atomics in 56, and register-space usage in 203. These categories can overlap.

## Evidence files

- `event-list-graphics-queue1.tsv`: event-list export selected for Graphics Queue 1 (3D Queue, GPU 0).
- `shader-map.csv`: PSO/stage, byte length, SHA-256, and shader-model mapping.

## Limits

- The event-list file itself does not identify its queue; the queue attribution comes from the PIX selection and exported filename.
- Findings are static analysis only. They do not prove visual equivalence or runtime compatibility.
- Raw `.wpix`, DXIL bytecode, and disassemblies are not included in this result package.
