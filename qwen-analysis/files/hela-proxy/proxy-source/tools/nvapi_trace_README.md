# NVAPI QueryInterface tracer

This diagnostic DLL logs each `nvapi_QueryInterface` ID and forwards it to the
original NVIDIA runtime. It does not mask or modify any NVAPI result. The
undocumented `nvapi_Direct_GetMethod` export is intentionally not exposed.
Build it explicitly with `cmake --build build --config Release --target
nvapi_trace`; it is excluded from the default build.

Build the `nvapi_trace` CMake target, then stage it app-locally beside the game
executable as `nvapi64.dll`. Save the installed NVIDIA DLL alongside it as
`nvapi64_real.dll`; do not replace the system copy. The tracer writes
`nvapi_query_trace.log` beside itself. Keep the original runtime in the same
directory while the tracer is installed. To roll back, remove the app-local
tracer and renamed copy; the system NVIDIA runtime remains untouched.

On the tested Hela installation, the game still loaded
`C:\Windows\System32\nvapi64.dll`, so the app-local tracer was bypassed and
must not be treated as an effective Hela workaround.
