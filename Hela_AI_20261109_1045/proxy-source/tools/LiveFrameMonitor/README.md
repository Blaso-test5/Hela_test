# Live Frame Monitor (`live_frame_monitor`)

Analizzatore diagnostico real-time (Python 3 standard library, zero dipendenze esterne) progettato per affiancare l'esecuzione diretta di **Hela** (`Hela-Win64-Shipping.exe`) e seguire in tempo reale `d3d12on11.log` e i dump BMP generati dal proxy D3D12-to-D3D11.

## Caratteristiche e Comportamento Esatto
- **Tailing resiliente**: attende la creazione di `d3d12on11.log` se non esiste ancora, legge solo righe complete terminate da `\n` (sfruttando `fflush(g_logFile)` del proxy C++), e rileva automaticamente **troncamento o rotazione** del file (`size < offset`), azzerando i contatori transitori e incrementando il contatore `generation`.
- **Correlazione Readback multi-header e interfogliata (`(kind, present)`)**:
  - Unisce automaticamente gli header multipli dello stesso `(kind, present)` (es. `[READBACK_SRC] Present #100 CopyResource source=...` seguito da righe di altri thread/shader e poi `[READBACK_SRC] Present #100 size=1280x720...`).
  - Riconosce sia le 5 righe campione native (`Top-left`, `Top-right`, `Center`, `Bottom-left`, `Bottom-right`) sia l'eventuale riga esplicita `Non-black pixels: X/Y (Z%)`, senza perdere il contesto quando righe di altri thread si interfogliano.
  - **Semantica Black / Alpha**: un campione RGBA è considerato **non-nero** se e solo se almeno uno dei canali colore visibili `(R, G, B)` è diverso da zero (`R != 0 or G != 0 or B != 0`), in pieno allineamento con l'ispezione BMP a 24/32-bit; `all_samples_zero` verifica invece che tutti e 4 i canali siano zero `(0, 0, 0, 0)`.
  - **Isolamento rigoroso del Present #100**: la sezione `present_100` del report espone esclusivamente i record del Present #100 (nessun fallback silenzioso a Present #200+ se il #100 manca), e `--stop-after-present100` termina non appena entrambi i record `#100` (`source` e `backbuffer`) sono completi (con attesa limitata a `--bmp-wait-timeout` se i BMP non vengono creati).
  - **Target Present configurabile**: `--stop-after-present N` (o `-StopAfterPresent N` dal launcher PowerShell) attende i readback source e backbuffer dello stesso Present; `capture_target` nel JSON riporta l'osservazione richiesta senza alterare la semantica rigorosa di `present_100`.
  - **Identita runtime della run**: `--run-manifest` carica un manifest versionato; il report espone versione, hash attesi/osservati per `d3d12.dll` e `dxgi.dll` e stato `pending`, `verified`, `mismatch` o `failed`. La rotazione/troncamento del log azzera le osservazioni runtime per non associare fingerprint della generazione precedente.
- **History dei draw**: accetta sia il formato legacy `VS=... PS=...` sia quello nativo `vs_hash=... ps_hash=...`, conservando tipo draw, conteggi, slot RTV e viewport (incluse le viewport native `(x,y,width,height)`).
- **Memoria limitata (Bounded Memory)**: utilizza ring-buffer (`collections.deque` / `OrderedDict`) con capacità fissa per draw sample per risorsa (max 20), record readback per Present (max 16, preservando Present #100), eventi `COPY_TO_BB` (max 16), heartbeat (max 10), indizi `Dispatch` (max 25) ed eventi live (max 100).
- **Ispezione atomica dei BMP**: quando vengono passati `--source-bmp` e/o `--backbuffer-bmp`, attende che la dimensione su disco raggiunga `pixel_offset + row_stride * height` prima di leggere e contare i pixel non-neri.
- **Separazione rigorosa Fatti vs Ipotesi**:
  - Una sorgente `[READBACK_SRC]` 100% nera e un backbuffer `[READBACK]` 100% nero provano che il nero si origina **a monte** di `CopyResource`, ma **non** provano la fedeltà di trasferimento di `CopyResource` (copiare zeri su zeri è indistinguibile da una copia non eseguita).
  - Un evento `SHADER PLACEHOLDER` per un Compute Shader prova l'assenza di bytecode SM5 utilizzabile per quell'hash, ma **non** prova da solo che il relativo `Dispatch` sia stato saltato né che l'output di quel CS alimenti il Render Target del Present #100.

## Istruzioni di Avvio e Arresto

### 1. Modalità Test / Post-Run su log esistente o fixture (senza avviare Hela)
```powershell
python tools/LiveFrameMonitor/live_frame_monitor.py `
  "D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-source-pixel-readback-20261110-125639\source-pixel-readback.log" `
  --once `
  --json tools/LiveFrameMonitor/out/live_present100.json
```

### 2. Modalità Live in tempo reale durante l'esecuzione di Hela
Per avviare il monitor prima di Hela e lanciare direttamente l'eseguibile (`-graphicsadapter=0 -windowed -ResX=1280 -ResY=720`, senza Steam, senza toccare DLL/INI, con quoting Win32 sicuro per percorsi con spazi e ripristino automatico delle variabili d'ambiente process-scoped anche su `Ctrl-C`). Ogni run deve avere un manifest `hela-run-manifest/v1` con `runtimeAttestation: module-and-shader-sha256`, SHA-256 attesi per entrambe le DLL e l'intero set di override; il launcher rifiuta l'avvio se i file installati non corrispondono e controlla che il log attesti le DLL e gli shader richiesti. Anche il report live riceve lo stesso manifest e attesta i fingerprint dei moduli osservati. I manifest storici senza attestazione runtime servono solo a documentare gli hash dei file, non sono validi per avviare una run:
```powershell
powershell -ExecutionPolicy Bypass -File tools/LiveFrameMonitor/Start-HelaLiveMonitor.ps1 `
  -LaunchGame `
  -RunManifestPath evidence/GTX_950M/runs/<run>/run-manifest.json `
  -EnableReadback `
  -EnableFullBmpReadback `
  -FullBmpPresent 1300 `
  -StopAfterPresent 1300 `
  -JsonOutPath tools/LiveFrameMonitor/out/live_run.json
```

`-EnableCrashDiagnostics` abilita in modo opt-in `D3D12ON11_CRASH_DIAGNOSTICS=1`
solo per il processo di gioco; il launcher ripristina il valore ambientale
precedente anche in caso di errore. Il crash handler crea report `.txt` e
minidump `.dmp` accanto al modulo proxy caricato. Nelle run gestite da
`ProxyManager.ps1`, i nuovi file vengono archiviati in `crash-diagnostics`
all'interno della cartella della run e lo stato dell'opzione viene registrato
in `manager-run.json`. Senza questa opzione il flag è impostato esplicitamente
a `0`.

`-FullBmpPresent` and `-StopAfterPresent` must be positive multiples of 100,
matching the native readback cadence. When full BMP readback is enabled, the
proxy captures both Present #100 and the requested Present (for example
#1300). The default BMP target is 100, preserving the original single-capture
behavior. Dumps are written to
`D:\HelaReadback_Present<number>_<source|backbuffer>.bmp`; the monitor's BMP
arguments only identify files to inspect and do not change these native paths.
`-StopAfterPresent100` remains available for a #100-only run. If a target is
requested without BMP paths, the monitor stops as soon as both target readbacks
are complete; supplied BMP paths receive a bounded additional wait.
For a no-override baseline, use `"overrides": {}` and `"requiredOverrides": []`
in the manifest and leave the installed override directory empty; the launcher
still verifies the runtime DLL fingerprints.

### 3. Proxy Manager (inventario, preflight, run riproducibili e restore)
`ProxyManager.ps1` centralizza il controllo del pacchetto e l'installazione temporanea.
`Inventory` elenca hash installati e pacchetti disponibili senza modificare file; `Prepare` verifica che
DLL, set di override e manifest coincidano; `Run` richiede conferma, crea una
cartella run univoca, salva un restore point byte-per-byte, archivia gli output
globali precedenti, avvia launcher e monitor sul Present richiesto con timeout
massimo (180 secondi di default) e ripristina in `finally` lo stato installato
precedente, che puo' gia' contenere DLL proxy iniettate. Il restore point resta nella run se serve
un recupero manuale. Il restore conserva anche lo stato iniziale in cui le DLL
proxy o la directory override non esistevano; `Restore` valida il restore point
prima di applicarlo e resta compatibile con i manifest di ripristino precedenti.

```powershell
$manager = "tools/LiveFrameMonitor/ProxyManager.ps1"
$version = "D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\versions\950m-v015-no-shader-override-srv-diagnostics-20261111"
$manifest = "evidence/GTX_950M/runs/20261111/950m-v015-no-shader-override-srv-diagnostics-20261111/run-manifest.json"
$runRoot = "evidence/GTX_950M/runs"

powershell -ExecutionPolicy Bypass -File $manager -Action Inventory
powershell -ExecutionPolicy Bypass -File $manager -Action Prepare `
  -VersionPath $version -ManifestPath $manifest -RunRoot $runRoot -Present 400
powershell -ExecutionPolicy Bypass -File $manager -Action Run `
  -VersionPath $version -ManifestPath $manifest -RunRoot $runRoot -Present 400 `
  -EnableCrashDiagnostics
```

`-RunRoot` e' obbligatorio per `Prepare` e `Run`, cosi' il manager non puo'
archiviare per errore una GPU nella cartella dell'altra. Per i test 1050 Ti,
impostarlo a `evidence/GTX_1050TI/runs`. I percorsi relativi sono risolti
rispetto alla root del progetto. Le run vengono salvate in
`<RunRoot>/<data>/<testVersion>-<UTC time>`; una collisione viene rifiutata,
mai sovrascritta. Se si usa una copia del manager esterna al tree Hela
(ad esempio la copia attiva in `Plugin Nuovi`), passare un `RunRoot` assoluto
verso la corretta cartella `evidence/GTX_*/runs`: il percorso relativo segue
la posizione del manager, non quella di un altro checkout Hela. Per un recupero esplicito:

```powershell
powershell -ExecutionPolicy Bypass -File $manager -Action Restore `
  -RunDirectory "evidence/GTX_950M/runs/<data>/<run-id>"
```

Non eseguire una nuova run finché il gioco è aperto. Il manager non cancella le
evidenze precedenti e non installa nulla in `Inventory` o `Prepare`.

I manifest possono inoltre definire un `launchProfile` opzionale con nome,
hash SHA-256 dell'ambiente ordinato, variabili `D3D12ON11_*`, argomenti e
marker di attestazione. Durante `Run`, ProxyManager risolve i token
`@RUN_OVERRIDES@` e `@RUN_DUMP@` in cartelle nuove dentro la run; il launcher
avvia Hela con un ambiente child esplicito dopo aver rimosso tutte le variabili
`D3D12ON11_*` ereditate. Il nome, l'hash, l'ambiente risolto e gli argomenti
vengono registrati in `manager-run.json`. I marker richiesti dal profilo
vengono verificati prima dell'attestazione degli shader; se mancano, la run
fallisce e il normale ripristino del ProxyManager resta attivo. I manifest
senza `launchProfile` mantengono il comportamento precedente.

### 4. Esecuzione della suite di test unitari
```powershell
python -m unittest discover -s tools/LiveFrameMonitor -p "test_*.py" -v
```

### 5. Snapshot diagnostico del Proxy
`Get-HelaProxySnapshot.ps1` ispeziona in sola lettura le DLL runtime e gli override
installati, confrontandoli facoltativamente con un run manifest o un restore point.
Non installa, ripristina o rimuove file. `-JsonOut` scrive solo il report e deve
indicare una destinazione esterna all'installazione e al restore point.

Il nome della directory degli override va specificato esplicitamente; per il layout
gestito da `ProxyManager.ps1` è `D3D12On11Patch\overrides`:

```powershell
$gameDir = "D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64"
$runManifest = "evidence\GTX_950M\runs\<data>\<run-id>\run-manifest.json"
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/LiveFrameMonitor/Get-HelaProxySnapshot.ps1 `
  -GameDirectory $gameDir -RunManifestPath $runManifest `
  -OverridesDirectoryName "D3D12On11Patch\overrides"
```

Per confrontare lo stato installato con un restore point:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/LiveFrameMonitor/Get-HelaProxySnapshot.ps1 `
  -RestorePointPath "evidence\GTX_950M\runs\<data>\<run-id>\restore-point" `
  -OverridesDirectoryName "D3D12On11Patch\overrides"
```

I manifest legacy senza `overrides_directory_present` assumono la directory
presente; non attestano un'assenza originaria. Exit code: `0` match/inventario,
`1` discrepanze, `2` input o I/O non validi, `3` confronto inconclusivo.
I test PowerShell autosufficienti (senza Pester) si eseguono con:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File tools/LiveFrameMonitor/Tests/Get-HelaProxySnapshot.Tests.ps1
```
