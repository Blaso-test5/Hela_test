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

## Hela-specific safeguards

- Check the current GPU and keep evidence separated under `evidence\GTX_950M` and `evidence\GTX_1050TI`.
- Always pass and verify the correct GPU-specific run output path. Do not rely on a Proxy Manager default that may point to another GPU's evidence directory.
- Run Hela directly through `Hela-Win64-Shipping.exe`; Steam is not required.
- Correlate shader, resource, draw, and Present readbacks from the same run. A forced-color diagnostic proves the output path only, not real-game rendering.
- Preserve injected proxy DLLs. Do not restore original DLLs unless explicitly requested; make a conservative ZIP backup before replacing injects.
- Keep diagnostics bounded and reversible. Do not infer a functional fix from a model suggestion without measurements and tests.
