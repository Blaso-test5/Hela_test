# Hela GTX 950M checkpoint — 2026-11-11

## Goal and status

Continue the controlled investigation until Hela renders a visible game image
on the GTX 950M. The goal is **not yet reached**. The v003 run still showed
black source and backbuffer readbacks despite 66 target draws.

The versioned proxy source now contains bounded, read-only D3D11 state
instrumentation for the next run. The `d3d12` target compiled successfully,
but the new DLL has **not** been installed or run with Hela. The previous
checkpoint remains at [`checkpoint-20261110.md`](./checkpoint-20261110.md).

## Diagnostic patch

The patch is in `../../proxy-source/src/commands.cpp`, immediately before
`DrawIndexedInstanced`. With `diagnosticRenderTrace` enabled, it records at
most the first four draws matching the v003 VS/PS hashes as
`[TARGET_DRAW_DIAG]` lines.

Each record includes draw arguments; proxy-tracked and actual context RTV0
resource pointers; DSV; vertex/index buffer bindings; index format/offset;
primitive topology; viewport/scissor counts and first entries; rasterizer
cull/scissor settings; blend enable, write mask, factor and sample mask; and
depth/stencil settings. The D3D11 getters only inspect state. Returned COM
references are released before the draw proceeds. Null state objects are
reported with D3D11's effective default cull, blend write-mask, and
depth/stencil values rather than zero-initialized descriptor values.

Argon's focused review confirmed the bounded-query approach and highlighted
the null-state defaults that were corrected. The changed code builds with
Visual Studio 18 / MSVC 19.50.35721.0 using the `d3d12` target. No CTest
smoke tests were run. The temporary build output was kept outside the
repository and is not included in this checkpoint; its SHA-256 is
`30277BFB0C8917CF5B257BD4CB75D69533FA34EB8B46039081812B75129FEDD9`.

## Run checklist

1. Recheck that Hela is closed and verify the installed DLL hashes; do not
   assume the November 10 installation state is unchanged.
2. Preserve v001 and v003. Stage the newly built DLL only in a new v004
   version directory; do not replace files while the game is running.
3. Keep the v003 fullscreen/magenta shader overrides unchanged so this run
   isolates the state instrumentation.
4. Run once and retain the live proxy log plus the Present #100 source and
   backbuffer readbacks. Confirm that `[TARGET_DRAW_DIAG]` appears and compare
   `contextRes0` with the actual `CopyResource` source and recorded history.
5. Treat the captured state as evidence, not proof that fragments reached
   memory. Choose the next intervention only after this correlation.
6. Publish the source and checkpoint changes with the next meaningful run
   evidence.

## Tooling decision

The existing Live Frame Monitor remains a CLI parser for the current log
format. A VS Code GUI could make live events and build comparisons easier to
inspect, but it would not answer the current draw-target/state question.
Defer the GUI until the instrumented run establishes which events and
comparisons are useful; keep the immediate effort on the bounded run.
