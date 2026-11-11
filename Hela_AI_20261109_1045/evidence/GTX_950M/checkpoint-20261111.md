# Hela GTX 950M checkpoint — 2026-11-11

## Goal and status

Continue the controlled investigation until Hela renders a visible game image
on the GTX 950M. The goal is **not yet reached**: the v004 test produced a
full-frame magenta image with forced shaders, not the game's normal visual
output.

The versioned proxy source now contains bounded target-draw diagnostics and
pipeline statistics. The v004 DLL pair was run once, then the game directory
was restored to v001. Hela is closed. The previous checkpoint remains at
[`checkpoint-20261110.md`](./checkpoint-20261110.md).

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

## GTX 950M v004 run — 2026-11-11

The new diagnostic DLL pair was run with the v003 fullscreen/magenta
overrides. Present #100 source and backbuffer readbacks are both uniformly
magenta: 921,600/921,600 non-black pixels each. The source resource matches
the recorded history (66 draws, zero skips, zero clears). Pipeline
statistics report 921,600 PS invocations for one override and 230,400 for
the other. The index values match between CPU shadow and D3D11 readback.

This proves the forced test path can render across the full GTX 950M
backbuffer; it does not prove that the real Hela pixel shader produces the
game image. The target PS has t0 bound to a 1x1 texture, but its texel value
was not captured. Full details and raw evidence are in
[`950m-v004-target-draw-state-20261111`](./runs/20261111/950m-v004-target-draw-state-20261111/README.md).

## Run checklist

1. Keep v001, v003, and the tested v004 pair intact; verify Hela is closed
   and confirm the installed hashes before changing anything.
2. Prepare a bounded readback of the 1x1 t0 texel in the existing target
   diagnostic. Keep only the fullscreen VS override for the next run so the
   original PS is exercised.
3. Run once, retain the live proxy log and Present #100 source/backbuffer
   readbacks, and verify the t0 value against the actual shader output.
4. Treat the diagnostic as evidence, not proof by itself. Choose the next
   intervention only after correlating the texel, PS statistics, draw target,
   and readback.
5. Publish the source and checkpoint changes with the next meaningful run
   evidence.

## Tooling decision

The existing Live Frame Monitor remains a CLI parser for the current log
format. A VS Code GUI could make live events and build comparisons easier to
inspect, but it would not answer the current draw-target/state question.
Defer the GUI until the instrumented run establishes which events and
comparisons are useful; keep the immediate effort on the bounded run.
