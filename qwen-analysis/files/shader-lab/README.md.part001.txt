# sm6-to-sm5-mcp

Server MCP per VS Code (Copilot Agent mode / qualsiasi client MCP) che aiuta a portare shader
**HLSL Shader Model 6.x → Shader Model 5.0**, per un backend **D3D11** al posto di D3D12.

## Installazione

```bash
npm ci
npm run build
npm test          # include test offline del job di precompilazione (FXC simulato)
npm run test:hela-package # gate offline di package Hela, senza DLL reali/rete
```

Apri la cartella in VS Code: `.vscode/mcp.json` è già pronto. Avvia il server da
*Comando: "MCP: List Servers" → sm6-to-sm5 → Start*, poi usa la chat in **Agent mode**.
Per usarlo in un altro workspace copia il blocco `servers` nel tuo `.vscode/mcp.json`
(con il percorso assoluto di `dist/index.js`).

## Dashboard laterale VS Code

Il progetto include anche `vscode-extension/`, un'estensione UI separata che aggiunge
MCP Dashboard alla Activity Bar con le schede **Shader Lab** e **Cattura PIX**. Per provarla:

1. Compila il server MCP con `npm ci` e `npm run build` dalla cartella principale.
2. Apri in VS Code la cartella principale `sm6-to-sm5-mcp`, così viene caricata la
   configurazione workspace `.vscode/mcp.json`.
3. Avvia `MCP: List Servers` → `sm6-to-sm5` → `Start Server`.
4. Installa il VSIX Shader Lab generato per questa versione oppure esegui
   `F5` nella cartella `vscode-extension/` per usare l'Extension Development Host.
5. Apri Shader Lab dalla Activity Bar e usa Copilot Chat in modalità Agent.

Il dashboard usa la configurazione MCP utente di VS Code (`User/mcp.json` o quella del profilo attivo);
il file workspace `.vscode/mcp.json` serve invece per avviare il server dal workspace. In caso di
percorso server memorizzato obsoleto, la root ricavata dal server effettivamente configurato nel profilo
ha priorità sul valore locale salvato dall'estensione.

Le azioni di analisi/conversione esistenti selezionano gli input e preparano una richiesta per il tool MCP
corrispondente in Copilot Chat. **Precompilazione DXIL locale** è invece un job posseduto dall'extension host:
seleziona una cartella di input, una base di staging separata e una mappa binding JSON opzionale, poi mostra
avanzamento e permette l'annullamento cooperativo. Gli output `.cso` sono scritti in una nuova sottocartella
`ShaderLab-Staging-*` e nominati con FNV-1a64 dei byte del container DXIL originale, come nel proxy.
File senza disassemblato, stage non riconosciuto, blocker SM6, entry point non identificato o register space
senza mappa non vengono compilati. FXC conferma solo la compilazione: non l'equivalenza, la compatibilità
della firma PS con il VS abbinato o il rendering. Gli output restano staging; la UI non li pubblica negli
override attivi e non avvia il gioco.

Il pill mostra se il server è compilato e associato al profilo; il controllo Live osserva il log MCP della finestra,
ricava l'ID completo usato da VS Code e apre le stesse opzioni server di
**MCP: List Servers** per `sm6-to-sm5`. Le righe di connessione aggiornano il pallino
senza aprire pannelli; se il log non esiste ancora, il primo clic apre l'elenco MCP.
La lettura dipende dal formato dei log della versione VS Code installata.

## Strumenti esterni (opzionali)

| Strumento | Serve per | Impostazione |
|---|---|---|
| `fxc.exe` (Windows SDK) | `compile_sm5` e precompilazione locale: compilazione reale SM5 | `FXC_PATH` (auto-rilevato su Windows) |
| `pixtool.exe` (PIX) | Analisi PIX completa ed export locale | Auto-rilevato o selezionato nella scheda Cattura PIX |
| `dxc.exe` (Windows SDK) | Disassemblaggio e analisi dei payload PIX | Auto-rilevato o selezionato nella scheda Cattura PIX |
| PowerShell | Decompressione locale dei payload PIX | PowerShell di Windows o percorso selezionato nella scheda |
| `dxil-spirv` | `dxil_to_sm5_hlsl` (shader senza sorgente) | `DXIL_SPIRV_PATH` |
| `spirv-cross` | idem | `SPIRV_CROSS_PATH` |

Non sono incorporati percorsi assoluti specifici di una workstation. Su ogni Windows Shader Lab
prova a rilevare PIX, DXC e PowerShell localmente; nella vista dedicata **Cattura PIX** si possono
selezionare gli `.exe` installati e salvare gli override nel profilo VS Code dell'utente.
Gli override non vengono condivisi con altri PC né scritti nel repository. `dxil-spirv` e
`spirv-cross` restano configurabili con le variabili MCP indicate sopra.

`check_toolchain` mostra cosa è raggiungibile. Le opzioni CLI di dxil-spirv possono cambiare
tra versioni: se il tool fallisce, controlla `dxil-spirv --help`.

## Tool MCP

- `analyze_shader` / `analyze_directory`: HLSL sorgente, report bloccanti / avvisi / correzioni automatiche.
- `analyze_dxil`: analisi statica di **DXIL compilato** (`.dxil` + disassemblato DXC): stadio, binding, register space, wave/64-bit/barycentrics/DXR/mesh, limiti D3D11, verdetto.
- `convert_shader` / `convert_directory`: riscrittura SM6→SM5 + `binding_map.json`.
- `compile_sm5`: compila con FXC per confermare che l'output è valido.
- `validate_pair`: confronta le firme output/input di un `.cso` VS e un `.cso` PS tramite `fxc /dumpbin`, verificando semantiche, maschere e tipi e restituendo PASS/FAIL.
- `dxil_to_sm5_hlsl`: DXIL → SPIR-V → HLSL 5.0 + post-processing (registri duplicati, register space, base instance, semantiche dei vertex input, entry point e `[numthreads]` compute ripresi dal disassemblato). Per un PS, `vsOutputDisasmPath` applica le semantiche della firma output del VS abbinato. Si ferma se l'analisi trova bloccanti. Output = *candidato*, da validare con fxc.
- `analyze_pix_capture`: `mode=scan` ispeziona il `.wpix` in sola lettura e con limiti espliciti; `mode=full` usa pixtool, estrae PSO e payload shader, li disassembla con DXC e salva report/manifest nella cartella scelta. La modalità completa può avviare un replay/export PIX, non modifica la cattura originale e non esegue il C++ generato.
- `list_pix_events`: esporta l'event list non filtrato con pixtool o legge un CSV/TSV locale; supporta paginazione, filtro sul nome e filtro esatto per il `Queue ID` di una singola riga. `Queue ID` non identifica la queue.
- `export_pix_draw_resource`: verifica che il `Global ID` identifichi una sola draw event e che la stessa riga abbia il `Queue ID` richiesto prima di invocare `save-resource`. Non esporta se l'identificazione è ambigua, il Queue ID non coincide o la riga non è una draw.
- `check_toolchain`.

### Integrazione con il proxy Hela

- `inspect_hela_proxy_log` analizza in sola lettura i log del proxy e separa shader/PSO, draw e readback; le correlazioni draw→backbuffer restano candidate e non sono una prova causale.
- `build_hela_override_stage` riusa la pipeline DXIL→HLSL→FXC e scrive solo in una nuova cartella di staging. Con un manifest baseline richiede tutti gli override baseline e ne verifica gli SHA-256; non installa file e non avvia Hela.
- Se `runtimeDir` è indicato al tool di staging, Shader Lab copia in staging `d3d12.dll` e `dxgi.dll`, aggiorna gli hash runtime solo nel manifest candidato, genera `candidate-package.json` ed esegue il gate offline `tools/hela-package/package-gate.mjs`. Il manifest baseline resta invariato; il gate verifica runtime/override, scrive `override-inventory.json` e `package-report.json` e mantiene sempre `lifecycle: staged-only`.
- `plan_hela_manifest_patch` produce un manifest candidato separato, preservando gli override baseline e rifiutando percorsi esistenti o protetti.
- `verify_hela_run` legge una run archiviata, usa il contatore readback per il target richiesto e considera solo BMP nominati per quel `Present`. Pixel non neri non dimostrano che il menu sia corretto: `menuValidated` rimane sempre `false`.
- `build_hela_run_evidence` collega offline package, manifest, run, log e BMP in una nuova cartella, applicando il package gate staged-only. Mantiene il verdetto menu `unverified`; non avvia Hela o non installa DLL/override.

## Catture PIX

La scheda **Cattura PIX** nel dashboard consente di scegliere il `.wpix`, la cartella di output,
