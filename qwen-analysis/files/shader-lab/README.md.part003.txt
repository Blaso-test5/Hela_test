gli hash VS/PS/GS/HS/DS e i dispatch compute con l'hash CS, raggruppati per esito, maschera degli
stadi mancanti e, per i draw, presenza del PSO graphics e RTV del backbuffer. I conteggi sono comandi:
uno `ExecuteIndirect` saltato è pesato con `maxCommandCount`, come il percorso eseguito che conta ogni
comando. La tabella traccia al massimo 512 gruppi complessivi e ogni riepilogo stampa al massimo 64
gruppi, dando precedenza a gruppi saltati o con stadi mancanti e riportando separatamente draw/dispatch
in overflow o omessi. Ogni riepilogo copre la finestra dall'heartbeat precedente; i contatori Present
restano process-wide: `present_calls_process_wide` e gli intervalli
`first/last_observed_present_count_process_wide` non sono identificatori di frame. La telemetria registra
i comandi osservati dal proxy ma non dimostra che uno shader abbia contribuito visivamente all'immagine.
Le chiamate `ExecuteIndirect` rifiutate per range non valido o backing resource indisponibile producono
log di errore ma nessun record shader, perché il proxy non emette quei comandi.
Le risposte `CheckFeatureSupport` riportano nomi, valori ed HRESULT nel log.
Il proxy traduce in modo limitato texture 2D reserved con layout 64 KB undefined-swizzle e buffer reserved
row-major, tile pool e mapping/copy delle tile via D3D11.2. Per i buffer tiled il backing D3D11 puo' essere
piu' piccolo della larghezza virtuale D3D12: il comportamento di tiling/mapping resta da validare sui driver.
L'implementazione corrente abilita questo percorso solo con D3D11 Tiled Resources Tier 2,
`ID3D11Device2` e `ID3D11DeviceContext2`, e riporta al massimo D3D12 Tiled Resources Tier 1. Un tier D3D11
inferiore causa `E_NOTIMPL`; non rimuovere il controllo senza verificare che le API D3D11 sottostanti
soddisfino le semantiche richieste. Texture 3D tiled e gli altri tier restano non supportati.
La V8 aggiunge diagnostica per le root SRV/UAV, limita le viste root alla larghezza del backing nativo
e rimuove i binding UAV/SRV precedenti quando un descriptor non è valido. Le viste raw sono rifiutate
se il backing non supporta `BUFFER_ALLOW_RAW_VIEWS`; le viste structured su risorse tiled sono rifiutate
finché dimensioni, mapping e copie non siano validati. Queste protezioni non rendono equivalente un
backing ridotto al buffer virtuale D3D12 e non costituiscono supporto completo per Hela.
`CreatePlacedResource` supporta risorse non multisample con allineamento e intervallo validi su heap
single-node; ogni risorsa riceve però backing D3D11 separato. Gli intervalli attivi sovrapposti sono rifiutati:
non sono implementati aliasing né backing condiviso dell'heap, e le risorse placed non equivalgono a heap
placement nativo.
`CreateCommandSignature` e `ExecuteIndirect` supportano una sola operazione indiretta di dispatch/draw
per signature, senza argomenti root e senza count buffer; restano non supportate le altre forme di
argomento indiretto.
`D3D12_FEATURE_FORMAT_SUPPORT` interroga il device D3D11 sottostante e converte i flag di supporto
formato; i flag D3D11 senza equivalente D3D12 sono esclusi e il supporto2 non disponibile resta a zero.
Per isolare il percorso NVAPI durante un test diagnostico, l'ambiente `D3D12ON11_DIAGNOSTIC_MASK_NVIDIA=1`
maschera il vendor dell'adapter NVIDIA nelle descrizioni DXGI; il vendor predefinito esposto è Microsoft
(`0x1414`), sostituibile per prove con `D3D12ON11_MASK_VENDOR_ID` (valore esadecimale, ad esempio
`FFFF` o `0`). La creazione del device continua a usare l'adapter D3D11 NVIDIA reale. Sono prove
temporanee, non impostazioni di compatibilità; i log riportano vendor/device ID nativi, vendor esposto
e l'adapter nativo passato alla creazione del device.
Per tracciare il modulo chiamante di alcune API D3D12, impostare `trace_callers=1` in
`d3d12on11.ini` oppure `D3D12ON11_TRACE_CALLERS=1` nell'ambiente del processo. Le righe `TRACE`
riportano API, modulo, RVA e thread nel log e in `OutputDebugString`; l'opzione è disattivata per default.
Il proxy espone anche `ID3D12Device1` e `ID3D12Device2`, richiesti dal controllo iniziale
dell'adapter D3D12 di Hela; questo non implica il supporto completo delle API aggiunte da tali
interfacce. I metodi non tradotti continuano a restituire `E_NOTIMPL` e sono registrati come `UNIMPL`.
L'opzione temporanea `diagnostic_capabilities=1` forza Resource Binding Tier 3, Tiled Resources Tier 3,
Wave Ops e atomiche Int64 su risorse tipizzate per verificare se il gioco supera il controllo iniziale.
È una simulazione deliberata di capability non implementate: usarla solo in test, poi riportarla a `0`;
non distribuirla né considerarla compatibilità reale. `ResourceBarrier` di transizione è ancora ignorato e viene segnalato. Il log
aiuta a individuare il primo blocco, ma non rende sicura l'esecuzione di un renderer non compatibile.
Nel bundle locale `d3d12on11/build/first-test` sono predisposti gli override SM5 per i campioni Hela
VS e CS e la mappa `b:3:0` → `b5`. È un pacchetto di diagnostica limitato: il PS Nanite del report
non è convertibile e gli altri shader non hanno override; non è una build compatibile con il gioco.
Il target `d3d12on11_shader_override_smoke` verifica il lookup degli override CS/VS con i DXIL
originali: copiando l'eseguibile accanto al bundle, lo si invoca con `cs|vs` e il percorso del
campione `.dxil`.

È ancora **un prototipo incompleto e non va installato come sostituzione delle DLL del gioco**:
transizioni delle resource barrier non sono tradotte; heap placement con aliasing/backing condiviso, i buffer e le
texture 3D tiled, molte interfacce e feature D3D12 recenti, nonché alcune dimensioni di viste/copie,
restano non supportate. Le copie con placed footprint sono implementate solo per formati e layout
validati dal proxy; i test coprono due formati non compressi e una copia parziale multi-riga, non
tutte le casistiche di formati compressi, box, array o texture 3D.
Il codice non converte DXIL in esecuzione: per ogni shader DXIL serve un override SM5 valido, e la
mappa dei register space deve coincidere con quella usata dal convertitore. Il test locale non
carica Hela e non dimostra la compatibilità del renderer reale. Servono ancora analisi dei log del
gioco e test dedicati prima di considerare il proxy utilizzabile.

Vincoli emersi dall'analisi dei campioni reali:
1. Gli shader convertiti usano `ByteAddressBuffer`; sono implementate viste raw per buffer, ma la copertura di tutte le forme di descriptor non è completa.
2. Le semantiche dei vertex input devono coincidere con l'input layout D3D12; il converter le ripristina dalla firma originale, ma vanno verificate con i PSO effettivi del gioco.
3. I register space devono usare la stessa mappa nel wrapper (root signature) e nella conversione (`bindingMapPath`).
4. Nanite, VSM e altre feature con atomiche a 64 bit / wave ops non sono traducibili: vanno disattivate nel motore.
