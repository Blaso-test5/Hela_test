un export C++ già esistente o l'export automatico con pixtool, e un event list PIX già esportato.
Include anche azioni per cercare eventi nell'event list ed esportare il render target di una draw.
Gli eseguibili sono rilevati sul PC corrente o collegati con i selettori nativi di VS Code.
La configurazione è per utente, quindi installare il plugin su un altro Windows non riutilizza
percorsi assoluti della workstation originale.

La modalità `scan` resta read-only: legge header e blocchi limitati, non decodifica il formato e non avvia PIX/pixtool. La modalità `full` scrive gli artefatti solo nella cartella selezionata; l'analisi dei payload è statica e non dimostra equivalenza visiva o compatibilità runtime D3D11. I marker `GFXA`/`GFXB`/`GFXC` e `DXBC` trovati dalla scansione non sono prova di chunk, PSO o shader. Pixtool esporta l'event list senza filtro queue; il `Queue ID` identifica la riga/evento nella queue, non la queue stessa. La label (per esempio `Graphics Queue 1`) resta un'attestazione visiva dell'utente e un event list manuale non prova la propria cattura o queue di origine. Prima di `save-resource`, il gate richiede una singola riga con `Global ID`, `Queue ID` coincidente e nome di evento draw.

## Cosa converte automaticamente

- `[shader("...")]`, `[WaveSize]`, `[[vk::...]]` rimossi
- `register(x, spaceN)` → slot univoci (con mappa dei binding da riportare nel C++)
- `ConstantBuffer<T>` → `cbuffer`
- `float16_t/uint16_t/int16_t` → `min16float/min16uint/min16int`
- `NonUniformResourceIndex(x)` → `x`
- `select(c,a,b)` → `c ? a : b`
- `dot4add_*8packed`, `dot2add` → funzioni helper di emulazione
- Negazioni unarie di scalari `uint` generati da SPIRV-Cross → sottrazione unsigned modulo 2^32 (evita warning FXC senza cambiare il risultato).

## Cosa NON è convertibile (viene segnalato come bloccante)

Wave/Quad intrinsics, DXR (ray tracing), mesh/amplification shader, `ResourceDescriptorHeap`,
interi/atomiche 64-bit, `SV_ViewID/Barycentrics/ShadingRate`, `GetAttributeAtVertex`,
`pack_*/unpack_*` 8-bit, `SampleCmpLevel/Bias/Grad`, feature HLSL 2021 (template, operator, ...),
array di risorse unbounded.

## Limiti da conoscere

1. **Gli shader sono solo una parte**: il gioco deve avere (o avere un wrapper con) un backend D3D11.
   Root signature, descriptor heap, barrier, command list e PSO non sono toccati da questo plugin.
2. Il converter lavora a livello di **sorgente testuale** (regex + parsing delle parentesi), non su un
   AST completo. `#include` e macro non vengono espansi: preprocessa prima (`fxc /P`, `dxc -P`).
3. I tipi `min16*` su D3D11 sono solo hint: la precisione può differire dall'originale a 16 bit nativi.
4. Limiti D3D11 non verificati automaticamente: 32 KB di groupshared, 8 UAV (pixel/compute 64 con FL11.1),
   dimensione dei cbuffer. `compile_sm5` ne intercetta alcuni.
5. La pipeline DXIL→HLSL produce codice decompilato: nomi persi, register derivati dai binding SPIR-V.
6. La precompilazione in batch richiede i disassemblati DXC associati ai file DXIL. La cancellazione è cooperativa e attende il processo esterno attivo; i test automatici simulano FXC e non provano il caricamento dal proxy.
7. `compile_sm5`, la precompilazione locale e il rilevamento automatico di fxc funzionano su Windows; su Linux/macOS serve Wine o un fxc esterno.
8. Testati: analyzer, converter, protocollo MCP, test offline del job con runner simulato e rigenerazione/validazione FXC dei campioni Hela.
   In questa workstation i due convertitori sono installati e configurati nel profilo MCP.

## Versioni usate nei test (vedi `samples/REPORT_Hela_samples.md`)

dxil-spirv commit `788f3e22968ee79d145a680279531389b439cb96` (`--ssbo-srv --ssbo-uav`), SPIRV-Cross commit
`57e4efcc84112abcd426dc5ec248b5339bee04f2` (`--hlsl --shader-model 50`), DXC v1.8.2505.1
(disassemblati forniti) e FXC del Windows SDK `10.0.26100.0`. Risultati per campione in
`samples/REPORT_Hela_samples.md`.
Con `bufferMode: "typed"` i buffer restano `Buffer<uint4>` in stile Vulkan: sconsigliato per D3D11.

## Stato dello Step 2 (proxy D3D12→D3D11) — PROTOTIPO PARZIALE

`d3d12on11/` compila ora due proxy Windows x64, `d3d12.dll` e `dxgi.dll`. Il livello D3D12 crea
un device D3D11, traduce risorse committed di base, heap descriptor e un sottoinsieme di command
list/queue; supporta root signature, binding e PSO quando gli shader sono DXBC SM5 o esistono
override `.cso` corrispondenti. Il livello DXGI inoltra le factory e crea wrapper per le swapchain,
inclusi `GetBuffer`, `Present`, `ResizeBuffers` e `ResizeBuffers1`. Gli altri export DXGI vengono
inoltrati alla DLL di sistema.
`GetTimestampFrequency` e `GetClockCalibration` sono implementati tramite query timestamp D3D11 e
QPC; la frequenza e' verificata con una query disjoint e memorizzata. La calibrazione e' una
correlazione best-effort tra il timestamp GPU e il campione QPC acquisito all'invio della query,
non una sincronizzazione hardware nativa dei clock D3D12.
Per il test del bootstrap, `D3D12CreateDevice` accetta richieste fino a feature level D3D12 12_0
e `CheckFeatureSupport(FEATURE_LEVELS)` riporta 12_0 anche se il backend sottostante è D3D11
11_0/11_1. Questa concessione sperimentale serve al bootstrap diagnostico e non implica che tutte
le feature D3D12 di livello 12_0 siano implementate; le capability non tradotte restano disattivate.
Hela richiede Shader Model 6.7, quindi il bundle locale la dichiara per superare il controllo iniziale.
È un'indicazione di compatibilità diagnostica: non aggiunge supporto SM6.7 al backend D3D11 e ogni
shader DXIL resta inutilizzabile senza un override SM5 valido.
Il proxy esporta inoltre `D3D12CreateDevice` e `D3D12GetDebugInterface` con gli ordinali attesi
dalle app e inoltra al runtime di sistema le due API di serializzazione delle root signature.

I test automatici coprono il dispatch compute con binding CBV/SRV/UAV e root constants, la
creazione di command signature e l'esecuzione di dispatch indiretti senza count buffer, copie
texture e readback, incluso upload/readback con placed footprint (`R32_UINT` e
`R8G8B8A8_UINT`) e copie parziali multi-riga tramite box in entrambe le direzioni, inclusi offset
di destinazione nel readback; una draw graphics SM5 su swapchain nascosta; creazione/query delle
factory DXGI; e il ciclo `GetBuffer` → `Present` → `ResizeBuffers`. Build e test su Windows:

```powershell
cmake -S d3d12on11 -B d3d12on11/build -A x64
cmake --build d3d12on11/build --config Release
ctest --test-dir d3d12on11/build -C Release --output-on-failure
```

`d3d12on11_queue_clock_smoke` verifica la frequenza, due campioni GPU/CPU monotoni e la gestione
dei puntatori nulli nelle API di clock.

Per i primi test con un'applicazione, il proxy registra le chiamate non implementate come
`UNIMPL`, i fallimenti di creazione delle risorse/PSO e il flusso di selezione degli shader nel file
`d3d12on11.log` accanto alle DLL. Se quella cartella non è scrivibile, usa
`%TEMP%\d3d12on11_<pid>.log`; il percorso scelto è scritto all'inizio del file. Le righe `SHADER`
riportano stadio, hash, formato e dimensione del bytecode, override cercato, eventuale dump DXIL e
risultato della creazione dello shader D3D11. `PSO ... BEGIN/READY/FAILED` delimita la creazione delle
pipeline. Un `SHADER BLOCKED ... no-usable-SM5-bytecode` indica uno shader da convertire o un override
assente/non valido; il file `.dxil` dumpato può essere passato agli strumenti `analyze_dxil` e
`dxil_to_sm5_hlsl` dello Shader Lab. Se il log non contiene `PSO ... BEGIN`, l'applicazione si è
fermata prima di richiedere la creazione di una pipeline e non è ancora possibile stabilire quali
shader restano da convertire. Il proxy non compila DXIL automaticamente: ogni shader DXIL necessita
di un override SM5 valido. Con `diagnostic_render_trace=1`, il proxy emette periodicamente
`[SHADER_USE_SUMMARY]`, `[DRAW_SHADER_USE]` e `[COMPUTE_SHADER_USE]`: registra i draw graphics con
