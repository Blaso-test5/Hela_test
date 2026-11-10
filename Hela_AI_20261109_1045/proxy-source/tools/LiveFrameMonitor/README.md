# Live Frame Monitor (`live_frame_monitor`)

Analizzatore diagnostico real-time (Python 3 standard library, zero dipendenze esterne) progettato per affiancare l'esecuzione diretta di **Hela** (`Hela-Win64-Shipping.exe`) e seguire in tempo reale `d3d12on11.log` e i dump BMP generati dal proxy D3D12-to-D3D11.

## Caratteristiche e Comportamento Esatto
- **Tailing resiliente**: attende la creazione di `d3d12on11.log` se non esiste ancora, legge solo righe complete terminate da `\n` (sfruttando `fflush(g_logFile)` del proxy C++), e rileva automaticamente **troncamento o rotazione** del file (`size < offset`), azzerando i contatori transitori e incrementando il contatore `generation`.
- **Correlazione Readback multi-header e interfogliata (`(kind, present)`)**:
  - Unisce automaticamente gli header multipli dello stesso `(kind, present)` (es. `[READBACK_SRC] Present #100 CopyResource source=...` seguito da righe di altri thread/shader e poi `[READBACK_SRC] Present #100 size=1280x720...`).
  - Riconosce sia le 5 righe campione native (`Top-left`, `Top-right`, `Center`, `Bottom-left`, `Bottom-right`) sia l'eventuale riga esplicita `Non-black pixels: X/Y (Z%)`, senza perdere il contesto quando righe di altri thread si interfogliano.
  - **Semantica Black / Alpha**: un campione RGBA è considerato **non-nero** se e solo se almeno uno dei canali colore visibili `(R, G, B)` è diverso da zero (`R != 0 or G != 0 or B != 0`), in pieno allineamento con l'ispezione BMP a 24/32-bit; `all_samples_zero` verifica invece che tutti e 4 i canali siano zero `(0, 0, 0, 0)`.
  - **Isolamento rigoroso del Present #100**: la sezione `present_100` del report espone esclusivamente i record del Present #100 (nessun fallback silenzioso a Present #200+ se il #100 manca), e `--stop-after-present100` termina non appena entrambi i record `#100` (`source` e `backbuffer`) sono completi (con attesa limitata a `--bmp-wait-timeout` se i BMP non vengono creati).
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
Per avviare il monitor prima di Hela e lanciare direttamente l'eseguibile (`-graphicsadapter=0 -windowed -ResX=1280 -ResY=720`, senza Steam, senza toccare DLL/INI, con quoting Win32 sicuro per percorsi con spazi e ripristino automatico delle variabili d'ambiente process-scoped anche su `Ctrl-C`):
```powershell
powershell -ExecutionPolicy Bypass -File tools/LiveFrameMonitor/Start-HelaLiveMonitor.ps1 `
  -LaunchGame `
  -EnableReadback `
  -StopAfterPresent100 `
  -JsonOutPath tools/LiveFrameMonitor/out/live_run.json
```

### 3. Esecuzione della suite di test unitari
```powershell
python -m unittest discover -s tools/LiveFrameMonitor -p "test_*.py" -v
```
