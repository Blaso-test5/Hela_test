# GTX 1050 Ti — follow-up del 12 novembre 2026

## Verifiche software

- Build proxy `Release`: riuscita.
- CTest: 7/7 test superati, incluso `d3d11_structured_buffer_copy_smoke`.
- LiveFrameMonitor: 15/15 test Python superati.
- Parser PowerShell: entrambe le copie di `ProxyManager.ps1` analizzate senza errori.
- `ProxyManager Prepare`: pacchetto aggiornato validato senza modificare l'installazione.

## Run live

### Build C++ aggiornata

- Pacchetto: `1050ti-merged-proxy-build-20261112`.
- Run: [manager-run.json](./1050ti-merged-proxy-build-20261112-111648166/manager-run.json)
  e [log proxy](./1050ti-merged-proxy-build-20261112-111648166/d3d12on11.log).
- Entrambi i fingerprint runtime corrispondono alla build attesa.
- Hela 5.6.1 termina prima di creare uno swapchain o raggiungere Present #100:
  non compare alcuna riga `SHADER READY`, e il manager non produce un report
  live o nuovi BMP.
- Il crash context locale riporta `EXCEPTION_ACCESS_VIOLATION` in scrittura a
  `0x0`; lo stack include `nvwgf2umx`, `nvapi64`, `GFSDK_Aftermath` e Hela.
  Questo localizza il percorso del crash, ma non ne dimostra la causa.

### Baseline con le DLL inject precedenti

- Pacchetto: `1050ti-installed-proxy-baseline-20261112`.
- Run: [manager-run.json](./1050ti-installed-proxy-baseline-20261112-112236807/manager-run.json)
  e [log proxy](./1050ti-installed-proxy-baseline-20261112-112236807/d3d12on11.log).
- Anche questo tentativo termina prima del Present; il proxy precedente non
  emette il fingerprint richiesto dal manager aggiornato, quindi la run non e'
  attestabile con questo manifest.
- Il crash context UE riporta lo stesso tipo di access violation; non e' stata
  stabilita una regressione specifica della nuova build.

Il log storico del 10 novembre raggiungeva Present #100, ma il source e il
backbuffer erano neri. Il confronto odierno non sostituisce quel risultato:
mostra che sul PC la sequenza di avvio e' cambiata o si interrompe prima del
rendering, senza ancora distinguere tra proxy, driver NVIDIA e stato del gioco.

## Integrita' e ripristino

- Prima dei test e' stato creato e verificato
  `pre-1050ti-merged-run-verified-20261112-121626.zip`, contenente le due DLL
  inject e i cinque override installati.
- Dopo entrambe le run, gli hash delle DLL e degli override installati
  corrispondono allo stato inject precedente; nessuna DLL originale del gioco
  e' stata installata.
- I BMP globali preesistenti sono stati ripristinati byte per byte; le copie
  conservate nei `previous-files` delle run fallite restano disponibili.
- Corretto `ProxyManager.ps1`: non tenta piu' di ricreare una root di drive gia'
  esistente durante il ripristino dei file. Il test di regressione e' incluso
  nella suite.
- Per la copia attiva del manager, `RunRoot` e' stato passato come percorso
  assoluto a `evidence/GTX_1050TI/runs`, evitando risoluzioni relative verso la
  directory proxy sbagliata.
