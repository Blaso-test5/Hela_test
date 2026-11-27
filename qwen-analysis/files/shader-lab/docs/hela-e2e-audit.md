# Hela / Shader Lab - audit end-to-end

## Stato implementato

- Shader Lab crea staging isolato, manifest candidato, package candidate, inventario e report.
- Il package gate verifica runtime, override, hash, path e file extra senza installare o avviare nulla.
- Il builder run-evidence collega package, run manifest, monitor, log e BMP solo offline.
- Il verdetto menu resta sempre `unverified`.
- Il proxy Hela non e stato modificato nel runtime; sono presenti solo tre artefatti offline:
  - `src/causal_trace.h`
  - `tests/causal_trace_offline.cpp`
  - `tools/LiveFrameMonitor/Assert-HelaPackageGate.ps1`

## Gap P0

### P0-A - package gate non consumato dal flusso runtime

`ProxyManager.ps1` deve usare il bridge PowerShell prima di preparare o avviare una run.
Il bridge deve verificare:

- `candidate-package.json` contro `run-manifest.json`;
- runtime d3d12/dxgi e relative SHA-256;
- set flat degli override;
- inventario ID/path/hash/bytes;
- package report `ok=true`, `lifecycle=staged-only`, no installazione e no rete.

Il cambio resta un prerequisito read-only. Non modifica installazione o avvio.

### P0-B - run senza identita comune

Il bundle run-evidence usa `manager-run.run_id` come identificatore comune. Deve
collegare manifest, report monitor, log, package e BMP della stessa directory.

### P0-C - correlazione draw/copy/readback non causale

I log correnti usano puntatori `ID3D11Resource*` riutilizzabili. `causal_trace.h`
introduce UID generazionali e valida UID, copy sequence, ordinale command-list,
fence e Present. Questo header non modifica il comportamento GPU finche non viene
cablato in `resource.cpp`, `commands.cpp` e `dxgi_proxy.cpp`.

## Primo slice successivo

1. Integrare `build_hela_run_evidence` nel server MCP.
2. Applicare il bridge in `ProxyManager.ps1` dopo `Assert-PackageMatchesManifest`,
   senza mutazione o launch.
3. Cablaggio futuro del run ID e degli UID nel proxy, solo dopo autorizzazione.
4. Test native di shader/load/barriere separati dai test visuali.
5. Checklist visiva esplicita prima di produrre `verified-with-checklist`.

## Verdetti ammessi

- `converted`
- `staged`
- `packaged-staged`
- `loaded`
- `present`
- `readback`
- `visual-checked`
- `menu=unverified`, salvo checklist esplicita e catena causale verificata.

## Verifiche eseguite

- Build TypeScript Shader Lab.
- Suite generale Shader Lab.
- 19 test package/evidence Node.
- Self-test PowerShell staged-only.
- Test C++ offline `causal_trace_offline.cpp`: tutti i casi passano.

Queste prove non dimostrano rendering, equivalenza semantica o funzionamento del menu.
