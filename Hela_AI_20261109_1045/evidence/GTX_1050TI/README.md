# GTX 1050 Ti evidence

The 2026-11-10 proxy investigation, measured results, diagnostic extracts,
lossless readback previews, and selected raw logs are in
[`runs/20261110`](runs/20261110/README.md).

This evidence is separate from [`GTX_950M`](../GTX_950M/README.md).

The November 17 pre-menu crash analysis is documented in
[`crash-analysis-20261117.md`](./crash-analysis-20261117.md). It distinguishes
the NVIDIA exception instruction from Hela's deeper stack frames and records
the failed shader-override attestation.

The isolated November 19 compute-candidate dispatch/readback test is in
[`compute-candidate-dispatch-20261119.md`](./compute-candidate-dispatch-20261119.md).
It verifies one candidate shader through the proxy on the GTX 1050 Ti host;
it is not a GTX 950M or in-game result.
