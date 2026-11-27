# NUOVO RISULTATO — menu raggiunto, crash alla cinematica

- **Run:** `resource-map-guard-ctxlock-profile14-20261127-225518266`
- **GPU:** GTX 1050 Ti
- **Data:** 27 novembre 2026
- **Esito:** menu parzialmente visibile e audio osservati; crash dopo l'avvio
  della cinematica. È il miglior avanzamento raggiunto, ma non una run stabile
  né un completamento del gioco.

## Sintesi

Questa run ha superato il limite precedente del pre-menu: l'utente ha visto il
menu parzialmente e sentito l'audio. Dopo l'avvio della cinematica, il processo
è terminato con un access violation READ su indirizzo nullo in
`d3d11.dll+0x96777`. Il launcher non ha richiesto la chiusura e non ha forzato
la terminazione. L'evidenza disponibile colloca il fault nel percorso D3D11,
ma non dimostra ancora quale chiamata del proxy abbia originato il puntatore
nullo.

La run precedente, conservata per confronto A/B, si era fermata prima del menu
con un crash nel percorso `memcpy` del readback. Il fault `d3d11.dll+0x96777`
è quindi distinto da quello precedente.

## Fatti verificati

- Il heartbeat ha raggiunto **Present #3286** prima della terminazione.
- Eccezione: `0xC0000005`, accesso `READ` all'indirizzo nullo.
- Modulo/offset: `d3d11.dll+0x96777`; lo stack prosegue in D3D11 e poi in
  `Hela-Win64-Shipping.exe`.
- Exit code del gioco: `0xC0000005` (`-1073741819`);
  `shutdown_requested_by_launcher=false` e
  `force_terminated_by_launcher=false`.
- Il proxy ha scritto **1.366** righe `RESOURCE_MAP` e nessuna
  `RESOURCE_MAP_SKIPPED`. Il crash precedente nel `memcpy` di
  `Resource::RefreshReadback` non si è ripetuto in questa run; questo è
  compatibile con il guard/lock aggiunto, ma non prova da solo la causa radice.
- Sono presenti i BMP source e backbuffer di Present #100 e #1500; i due file
  di Present #1500 hanno dimensione **2.764.854 byte** ciascuno e hash uguale.
  L'uguaglianza degli hash non prova che l'immagine sia corretta.
- Il manager ha marcato la run `failed` perché il live monitor non ha
  completato la validazione source/backbuffer per Present #1500, nonostante i
  due BMP siano presenti. Il report del monitor indicava
  `capture_target.complete=false`: c'è quindi una discrepanza tra gli
  artefatti scritti e la validazione del monitor da investigare.
- Nel profilo era impostato `D3D12ON11_DUMP_SHADERS=0`. Non sono stati prodotti
  nuovi dump shader in questa run.
- Riepilogo shader del monitor: **16 shader ready**, **256 placeholder
  distinti**, **6.861 eventi placeholder**. Sono contatori di osservazione
  proxy, non prova del completamento GPU né della contribuzione visiva al menu.
- Il package gate è passato in modalità `staged-only`: **17 override**
  presenti, **13 richiesti**. Il package non ha modificato direttamente
  l'installazione.

## Patch e verifica

La modifica a [`resource.cpp`](../../../../../proxy-source/src/resource.cpp)
serializza l'uso del contesto D3D11 durante
`CopyResource/Map/memcpy/Unmap`, registra il risultato di `Map` e verifica
gli intervalli di memoria sorgente e destinazione prima della copia. In questa
run non è stato registrato alcun `RESOURCE_MAP_SKIPPED`.

Verifiche eseguite prima della run:

- build Release del target `d3d12on11_smoke`: **passata**;
- CTest `d3d12on11_smoke`: **1/1 passato**;
- package gate per `resource-map-guard-ctxlock-profile14-20261127`:
  **passato**.

Il candidato staged aveva SHA-256 D3D12
`0bad046d0cf421b3141f234db8422c363b54bf379757502cf265736660c878a5` e DXGI
`808be0b95b836b8f8a7901ff8c216c73f6d764e804dcb9c631115dc79cf3815f`.
Al termine è stato ripristinato lo stato installato: D3D12
`23e2c8ba52a1cba32d01595c505e047b21b2744a3df807cade734f648ccd7fbe`, DXGI
`edd099d49c4b669c05e1fd46f4af0e251cbee7065a9d596aa93c6d4fafca4aad`, con
cinque override ripristinati.

## Profilo per riprodurre il test

Il [`run-manifest.json`](./run-manifest.json) contiene gli hash degli
override e il profilo senza percorsi macchina. I parametri principali della
run sono riportati sotto. Il [bundle di riproducibilità sanitizzato](./replication-evidence/README.md)
include il log proxy completo compresso, i report JSON redatti e lo stack crash
con soli offset di modulo.

```text
D3D12ON11_TILED_RESOURCES_TIER=3
D3D12ON11_DIAGNOSTIC_CAPABILITIES=1
D3D12ON11_DIAGNOSTIC_RENDER_TRACE=1
D3D12ON11_MASK_VENDOR_ID=ffff
D3D12ON11_TARGET_DRAW_CAPTURE_RESOURCE_HIT=400
D3D12ON11_DUMP_SHADERS=0
D3D12ON11_DATAFLOW_TRACE=1
D3D12ON11_TRACE_CALLERS=0
D3D12ON11_DIAGNOSTIC_MASK_NVIDIA=1
D3D12ON11_SHADER_MODEL=6_7
D3D12ON11_OVERRIDES=<RUN>\launch-profile\overrides
D3D12ON11_DUMP=<RUN>\launch-profile\dump
```

Argomenti:

```text
-graphicsadapter=0 -windowed -ResX=1280 -ResY=720
-stdout -FullStdOutLogOutput -forcelogflush
```

Per la GTX 950M usare un percorso evidenze separato
(`evidence/GTX_950M`), verificare l'adapter effettivamente selezionato e non
assumere che il masking NVIDIA o il package della 1050 Ti siano
intercambiabili senza un nuovo package gate. Questa run non ha generato shader
del menu: per una raccolta dedicata abilitare il dump solo nella nuova run e
fermarsi al menu, prima della cinematica.

## Analisi del crash: limiti attuali

Ghidra ha completato l'import di `d3d11.dll`, ma non ha trovato una funzione
che contenga l'indirizzo `+0x96777`; la copertura di disassemblaggio riportata
dal worker è zero. Non c'è quindi ancora una decompilazione utilizzabile e
non si attribuisce il crash a una riga specifica del proxy.

La priorità della prossima patch è correlare il crash dump completo con le
chiamate D3D11/D3D12 immediatamente precedenti e correggere la discrepanza del
validator per Present #1500. Non cambiare più variabili contemporaneamente:
prima riprodurre il menu senza avviare la cinematica; poi fare una run separata
con shader dump abilitato.

## Retention e artefatti

Pulizia completata sull'inventario delle cartelle con `manager-run.json`:
da **27** run sono state mantenute questa run migliore e la run
`resource-map-guard-profile14-20261127-224204416` come controllo A/B pre-menu;
le altre **25** cartelle sono state rimosse. Dei sei staged package ne restano
tre: il baseline `proxy-main-4450b7d-1050ti-profile14-menu-3graphics-dataflow-20261125`
e i due stage guard. Dei due backup è rimasto il più recente,
`pre-resource-map-ctxlock-20261127-235509.zip`.

La pulizia è stata limitata ai run indicizzati, ai tre stage obsoleti e al
backup guard precedente. Cartelle standalone e profili non inclusi
nell'inventario non sono stati toccati.

I BMP del gioco, il minidump e i file shader `.cso` restano locali e non sono
inclusi nel repository. Il log proxy completo è allegato solo come copia
sanitizzata, con i percorsi macchina rimossi; stdout/stderr del gioco non sono
stati inclusi.
