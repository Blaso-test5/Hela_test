---
name: bridgecode-arena-proxy-workflow
description: "Use when working on the Hela D3D12-to-D3D11 proxy, Proxy Manager, render diagnostics, BridgeCode MCP, Arena coding models, generated plugins, or Arena Code worktrees."
---

# BridgeCode / Arena workflow for the Hela proxy

Use this workflow for proxy, Proxy Manager, shader-conversion, and Hela-render investigation tasks. BridgeCode is a required second-model/tooling path for this work, not a substitute for checking the repository, local runtime, tests, and measured GPU evidence.

## 1. Check Arena before sending anything

1. Call BridgeCode status first. Confirm the active page/chat, browser readiness, busy state, current model, and mode.
2. If a chat is active, read its context and last response before composing a new prompt. Do not interrupt an unrelated or still-running user request.
3. If BridgeCode is busy, do not repeatedly poll, send another prompt, or switch its model underneath the running request. If the operation is taking too long, stop and tell the user what remains so they can resume it.
4. Read the available model categories, exact model IDs, and modes. Never invent an ID.

## 2. Research and select a coding model

1. For a model-selection request, check current 2026 coding evidence on the web before choosing: prefer task-relevant coding benchmarks and verified leaderboard results (for example SWE-bench Verified, LiveCodeBench, or an Arena coding leaderboard), and note the date and source.
2. Compare only models actually listed by BridgeCode in the active category. Treat Arena display names as candidates, not proof of benchmark identity or rank.
3. Prefer a strong reasoning/coding model with evidence relevant to repository-level edits; consider reliability and context capacity as well as a single benchmark score.
4. If public rankings are inaccessible or cannot identify the exact Arena model, say so plainly and do not claim a verified “best.” Use the best-supported available candidate only after checking the exact ID.
5. Select the appropriate category (Text or Code), then select the model by its returned ID, and verify the selection. Do not change a busy chat.

## 3. Choose Text or Code deliberately

- **Text mode:** use for a bounded code review, diagnosis, design discussion, or patch advice. Send only the relevant excerpts, not entire source files. Include file path, line range, minimal surrounding context, observed facts, the precise question, and constraints.
- **`@file`:** use only when Arena can access the file and its size is comfortably within current chat limits. Prefer short excerpts when uncertain.
- **Code mode:** use when a complete new plugin, tool, or multi-file worktree is genuinely useful. Give an isolated output directory, requirements, build/test commands, and expected deliverables. Expect longer generation time.
- Before asking Arena to write files, check the project state and define how generated output will be reviewed and merged. Do not let generated files overwrite active source or injected runtime DLLs directly.

## 4. Budget prompts and review output

1. Minimize tokens intelligently: summarize established facts, include only discriminating excerpts, and state what the model must not re-investigate.
2. Observe the actual Arena character/context limit or any rejection message; do not assume a fixed limit. Split only independent questions, not one continuous trace.
3. Ask for concrete hypotheses or changed files, rationale, risks, and validation steps. Avoid open-ended requests for entire source rewrites when a surgical patch is sufficient.
4. Treat model output as a proposal. Compare it with the local source and tests; reject unsupported assumptions and retain a traceable copy of accepted generated artifacts.

## 5. Inspect Code-mode worktrees and artifacts

1. Use BridgeCode MCP to enumerate available Code artifacts and files before reading them.
2. Inspect the artifact in staging, including its file tree, source, build instructions, and archive contents where available. Do not execute generated binaries or scripts before review.
3. Build and test in an isolated worktree or directory first. Record the artifact identifier, files reviewed, test results, and any merge decisions.
4. Merge only the necessary files into the project, preserving local changes and the active injected DLLs. Create the user's requested conservative ZIP backup before replacing runtime DLLs.

## 6. Diagnose BridgeCode MCP timeouts and duplicate sends

- Treat a BridgeCode slow-tool notification as evidence of the current stage, not as proof that the operation was cancelled. Do not retry the same MCP call while it may still be running; concurrent searches can duplicate work and make an index build slower.
- For `bridgecode_search_code` warnings in `caricamento dell’indice workspace`, first establish which VS Code workspace and chat/project root the failing MCP client uses. `bridgecode_status` reports the active BridgeCode browser session, not necessarily the separate chat or workspace that produced another client's timeout.
- The project index is cached by workspace root for the lifetime of the extension host. A different chat with the same root should reuse it once built; a different `session.projectRoot`, an uncached project root, or simultaneous first requests can trigger another full scan.
- `FileIndex.buildAsync()` still walks eligible directories and reads every eligible file up to 200 KB to create previews. The 25 MB search budget applies after indexing, so it does not cap or accelerate the initial index build. Excluded directories/extensions are defined in `ContextBuilder.ts`.
- If the warning persists, record the exact stage, workspace root, whether this is the first search after selecting that root, and whether concurrent MCP requests were launched. Avoid repeated `bridgecode_list_files` or search calls as a diagnostic; inspect local logs and the selected project configuration first.
- Never infer that a prompt was unsent from a timeout alone. Check BridgeCode status and read the last response before considering a retry. If generation is active, wait; do not resend or cancel without explicit user direction.
- To verify duplicate-send prevention, run one short, harmless prompt in an isolated/free chat, then check the user/assistant message counts and final response. A successful single MCP test and a separate manual GUI test showed one user message and one reply each; this is evidence for those runs, not a guarantee for every model, network error, or Arena state.

## 7. Resume work in a blank Arena chat

- If the user asks to continue and status shows a ready browser but no active chat, do not ask them to recreate the context. Compose a compact handoff from the current worktree and verified session notes, then use `bridgecode_switch_to_new_chat` to create a fresh chat and send one focused prompt.
- Include the project goal, current change, tests already run, limits/constraints, and the precise question. Attach only the smallest relevant excerpts from real project files; do not send entire files by default.
- For `files`, search first with `bridgecode_search_code`, then read the exact same paths and line ranges with `bridgecode_read_excerpt`. For `codeFiles`, read those exact ranges with `bridgecode_read_active_code_file` from the active Code tab. If a new file is absent from the index or Code is not active, state that clearly and summarize its relevant logic in the handoff instead of claiming it was attached.
- If status says busy, or a send is unconfirmed/blocked, do not open another chat or retry. Check the current status and last response once; if the request cannot proceed, stop and report what is missing.
- Keep game-captured shader outputs (for example HLSL or `.cso`) out of Arena prompts unless the user explicitly authorizes those specific artifacts. Prefer proxy/tool source excerpts.
- When a new, repeatable Hela/Arena action or correction is established and the user wants it remembered, update this skill and the Hela repository copy if present. Record only verified steps, constraints, and outcomes; do not preserve secrets or raw game shader artifacts.

## 8. Shader-use ledger review lessons

- Treat ledger entries as proxy draw/dispatch calls observed, not proof of GPU completion or visual contribution. Present values are process-wide counters, not frame identifiers; an RTV pointing at the backbuffer is not proof that the draw affected its contents.
- Define and label the count unit for `ExecuteIndirect`: executed paths can record each embedded command, while a skipped call may only be known as one API invocation. Do not compare those counts as if they were equivalent; count skipped commands only when the count is known.
- Check every early return in direct and indirect execution paths. Rejected arguments or unavailable backing resources are not shader executions; either record a separate diagnostic counter/log or document the gap.
- Mark snapshots as windows since the previous dump when a heartbeat resets the ledger. Do not label window totals as cumulative process totals.
- A top-N summary can hide rare skipped or missing-stage groups. Prefer those groups when truncating, and expose omitted/overflow totals. Test the invariant per execution type: tracked entries + omitted entries + overflow equals the reported total.

## Hela-specific safeguards

- Check the current GPU and keep evidence separated under `evidence\GTX_950M` and `evidence\GTX_1050TI`.
- Always pass and verify the correct GPU-specific run output path. Do not rely on a Proxy Manager default that may point to another GPU's evidence directory.
- Run Hela directly through `Hela-Win64-Shipping.exe`; Steam is not required.
- Correlate shader, resource, draw, and Present readbacks from the same run. A forced-color diagnostic proves the output path only, not real-game rendering.
- Preserve injected proxy DLLs. Do not restore original DLLs unless explicitly requested; make a conservative ZIP backup before replacing injects.
- Keep diagnostics bounded and reversible. Do not infer a functional fix from a model suggestion without measurements and tests.
- Review Hela launch stdout before retaining or sharing it: the embedded Sentry reporter can print `sentry_key` query values. Redact them before archival; never send raw stdout to Arena or commit it.
- To disable Hela's Sentry plugin auto-initialization for the current Windows user, set `InitAutomatically=False` under `[/Script/Sentry.SentrySettings]` in `%LOCALAPPDATA%\Hela\Saved\Config\Windows\Engine.ini`. Preserve and merge any existing file. This is reversible and does not alter game DLLs; it only prevents automatic initialization, so verify the next safe launch log before claiming reporting is stopped. Do not delete `CrashReportClient.ini` preferences or Crashpad binaries as a substitute, and do not claim this retracts earlier events.
- To audit past reporting, count `sending envelope` and HTTP status lines in archived stdout and inspect request `Content-Length` and local envelope item type without exposing DSNs or `sentry_key`. HTTP 200 confirms the endpoint accepted that envelope, not the contents of every event or minidump. Check `%LOCALAPPDATA%\Hela\.sentry-native` for retained reports; do not infer they were sent or delete them without confirmation, and do not rerun Hela solely to check delivery.
- For outbound audits, an `ESTABLISHED` TCP connection confirms a connection, not its application payload; a cached DNS hostname is suggestive, not proof of the hostname used in that run. Without a packet capture or application request log, record the payload as unknown.
- If an online graphics run is authorized, start `tools\Watch-HelaTcp.ps1` in a separate PowerShell window before launch and write to a fresh, run-specific `network-observations.jsonl`. It persists observed Hela/Crashpad TCP state transitions independently of the run summary. It does not capture payload or UDP, and no observed connection is not proof that no data was sent.
- The proxy's placed-resource overlap mitigation accepts overlapping D3D12 placements but creates independent D3D11 backing resources; aliasing and UAV barriers remain unimplemented. Treat this as a startup compatibility mitigation only, and require a same-run GPU and visual test before claiming rendering is fixed.
- If zero external connectivity is required for a Hela run, disconnect Internet-capable interfaces or verify an effective policy block first. When local firewall rules may be filtered by policy, confirm the exact executable rules appear in `ActiveStore`; a `PersistentStore` entry alone is insufficient. Keep the PC offline until Hela and Crashpad exit, since reports may remain queued for later.

### Qwen review through the Hela repository

- When the user authorizes online source review, provide `https://github.com/Blaso-test5/Hela_test` and the current branch/commit, then name the exact proxy files Qwen should inspect. Avoid pasting entire source files when the repository is accessible.
- For black-screen diagnosis, prioritize `Hela_AI_20261109_1045/proxy-source/src/commands.cpp`, `types.h`, `dxgi_proxy.cpp`, `pipeline.cpp`, `device.cpp`, `resource.cpp`, and `proxy-source/tests/smoke.cpp`; point Qwen to `README.md` for measured context.
- Ask for an evidence-based diagnosis and one bounded source-level change or discriminating test. State which conclusions the current readbacks and shader ledger do not prove.
- Treat repository contents as current only through the reported commit. Do not claim Qwen read a file unless its response demonstrates that.
- Do not publish raw launch logs, Sentry-bearing stdout, captured shader HLSL/CSO, or game screenshots/readbacks unless the user explicitly authorizes those specific artifacts. Prefer sanitized measurements and source paths.
- Review every proposed edit locally. Keep changes in the proxy source, run build and native tests, and never write generated changes directly over installed runtime DLLs.

### Reproducing a known-good Hela run

- Reproduce the complete recorded launch profile, including environment variables and exact runtime DLL hashes; matching only the DLLs is not sufficient.
- Stage each shader override in a run-specific directory before launching a fresh Hela process. Set `D3D12ON11_OVERRIDES` and `D3D12ON11_DUMP` to run-specific absolute paths and set `D3D12ON11_DUMP_SHADERS=0` when matching a profile that disabled shader dumping. Never overwrite or clean the shared patch dump as a side effect of a test.
- Remove or back up external readback destinations before launch, retain a run-local copy of every newly written BMP even when its hash matches an earlier image, and restore the original external files afterward. The run log and write time establish freshness; equal image hashes do not mean the capture is stale.
- Match the proxy's actual log syntax when detecting captures (for example, `[READBACK_FULL] Present #100 saved READBACK_SRC`). Distinguish the full-frame BMP measurement from the proxy's later sampled-point percentages; they are not interchangeable.
- Treat `SHADER READY` for each target, successful Present, fresh readback, pixel result, process exit code, and restoration as separate facts. Do not infer a slot or root cause from a shared black #100 image, unequal run durations, or an unexplained nonzero exit code.
- After every run, verify the game-root DLL state, shared override hashes/count, original log/readbacks, and that no Hela process remains.
