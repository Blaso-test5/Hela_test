$manager = "tools/LiveFrameMonitor/ProxyManager.ps1"
$version = "<HELA_INSTALL_ROOT>\Binaries\Win64\D3D12On11Patch\versions\950m-v015-no-shader-override-srv-diagnostics-20261111"
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
$gameDir = "<HELA_INSTALL_ROOT>\Binaries\Win64"
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
