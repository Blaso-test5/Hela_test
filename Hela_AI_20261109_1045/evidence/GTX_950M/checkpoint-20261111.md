# Hela GTX 950M checkpoint — 2026-11-11

## Goal and status

Continue the controlled investigation until Hela renders a visible game image
on the GTX 950M. The goal is **not yet reached**. The v003 run still showed
black source and backbuffer readbacks despite 66 target draws.

The versioned proxy source now contains bounded target-draw diagnostics and
pipeline statistics. The merged `d3d12` target compiled successfully, but
the new DLL has **not** been installed or run on the GTX 950M. The previous
checkpoint remains at [`checkpoint-20261110.md`](./checkpoint-20261110.md).

## Diagnostic patch

The helper in `../../proxy-source/src/commands.cpp` records at most six
matching draws overall and at most two per RTV resource. It logs the actual
RTV/resource against the most recent CopyResource source, draw arguments,
VB/IB bindings and index data, CBV/SRV/sampler bindings, viewport/scissor,
rasterizer, blend, and depth/stencil state. Bounded pipeline-statistics
queries report whether vertex and pixel shader invocations occurred.

The diagnostics also copy a small amount of bound buffer data to staging
resources for readback and issue pipeline-statistics queries. They do not
clear or write the render target, but may perturb timing. Null depth state is
reported with D3D11's effective default depth enable/function/write mask;
blend factors are logged explicitly.

Argon's focused review highlighted the default-state ambiguity that was
corrected. The merged source builds with Visual Studio 18 / MSVC
19.50.35721.0 using the `d3d12` target. No CTest tests were run for this
GTX 950M build; the upstream GTX 1050 Ti report records 6/6. The temporary
DLL was kept outside the repository and is not installed. Its SHA-256 is
`76AF76317A089CE153AEB101BEDDD48C9F5F3C4EDFDDD112757ACDFDB8D2DB4F`.

## GTX 1050 Ti evidence from the merged update

- The target VS and PS ran: the converted-PS test recorded 1,044,480 PS
  invocations, but the actual-shader source/backbuffer readbacks remained
  black.
- A magenta PS override produced a full magenta readback; therefore the
  target can receive pixel output on that GPU.
- The target PS has t0 bound to a 1x1 `R8G8B8A8_UNORM` view and a sampler.
  The root-CBV bytes matched the bound D3D11 buffers. The missing
  discriminating value is the sampled texel content at draw time.
- These are GTX 1050 Ti results, not proof of the GTX 950M's behavior.

## Run checklist

1. Recheck that Hela is closed and verify the installed DLL hashes; do not
   assume the November 10 installation state is unchanged.
2. Preserve v001 and v003. Stage the merged build only in a new v004 version
   directory; do not replace files while the game is running.
3. Keep the v003 fullscreen/magenta shader overrides unchanged so this run
   isolates the state instrumentation.
4. Run once and retain the live proxy log plus the Present #100 source and
   backbuffer readbacks. Compare the logged RTV/resource with the most recent
   CopyResource source and check `TARGET_DRAW_STATS` for shader invocations.
5. If the readbacks remain black, use the logged t0 resource identity to plan
   a bounded texel readback; do not infer the texture contents from its
   dimensions or SRV binding alone.
6. Treat the captured state as evidence, not proof that fragments reached
   memory. Choose the next intervention only after this correlation.
7. Publish the source and checkpoint changes with the next meaningful run
   evidence.

## Tooling decision

The existing Live Frame Monitor remains a CLI parser for the current log
format. A VS Code GUI could make live events and build comparisons easier to
inspect, but it would not answer the current draw-target/state question.
Defer the GUI until the instrumented run establishes which events and
comparisons are useful; keep the immediate effort on the bounded run.
