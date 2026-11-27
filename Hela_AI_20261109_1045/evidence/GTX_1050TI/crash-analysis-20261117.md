# GTX 1050 Ti crash analysis — 2026-11-17

## Result

Two Unreal minidumps created during the 23:13 and 23:22 local-time managed
runs report the same exception:

- `EXCEPTION_ACCESS_VIOLATION` (`0xC0000005`), writing to address `0x0`.
- The exception instruction pointer is `nvwgf2umx.dll + 0x564B98`
  (`0x7FFBC2194B98`), not an instruction in Hela.
- Both dumps record NVIDIA driver file version `32.0.15.8278`. The dump's
  module timestamp and checksum match the driver binary currently installed
  on the analysis machine.
- The recorded stack includes `nvapi64.dll`, NVIDIA Aftermath, and Hela
  frames. This establishes where the exception was reported, but does not
  establish whether the underlying cause is in the driver, Hela, or an
  upstream call/resource state.

The two runs were `1050ti-pre-menu-baseline-14-20261117-221337441` and
`1050ti-pre-menu-baseline-14-20261117-222207333`. Both reported
`status=failed` because the expected shader override was not attested, and
both reported `restored=true`. Therefore these runs do not validate the
intended override profile. Their raw run folders were removed in the
2026-11-27 retention cleanup; the technical conclusions needed for this
analysis remain summarized here.

## Hela stack interpretation

The Hela stack frame at RVA `0x25C32D9` maps to the instruction immediately
after an indirect `CALL RAX` at RVA `0x25C32D7`, in `FUN_1425c3260`. A wrapper
at RVA `0x2FAABBF` maps to `FUN_142faab90`, which calls that function. The
minidump does not contain the runtime indirect-call target, so the callback
implementation cannot be identified from these artifacts alone.

RVA `0x14BD97F` also appears deeper in the Hela stack, but disassembles as
`MOV EAX,1`; it is not the faulting instruction and does not write to memory.
The exception address and the Hela stack frame addresses must not be conflated.

The two functions above were reconstructed from their PE unwind extents in
the existing Ghidra worker project and their disassembly/decompilation was
verified through Ghidra MCP. No additional worker was created. This analysis
did not modify the game executable or start another Hela run.

## Evidence limits

Raw minidumps and full crash contexts are intentionally not included: they
can contain process memory and machine/account identifiers. The crash folders
remain local. No claim is made that the driver alone caused the crash, that
the intended shader override was tested, or that Hela reached a rendered
scene or menu.
