# GTX 1050 Ti — risultati del 10 novembre 2026

## Ambito

Run locali di Hela Demo sulla NVIDIA GeForce GTX 1050 Ti (`10DE:1C82`),
avviato direttamente da `Hela-Win64-Shipping.exe`. Queste prove sono
separate dalla cartella `GTX_950M`.

Build Release del proxy riuscita; CTest: 6/6 test superati. Il sorgente
strumentato e' in
[`commands.cpp`](../../../../proxy-source/src/commands.cpp).
L'HLSL convertito del PS reale e' in
[`44b0a992c2304254.converted.hlsl`](../../../../shaders/hlsl/44b0a992c2304254.converted.hlsl).

## Risultati in ordine cronologico

| Ora | Run / prova | Esito osservato |
|---|---|---|
| 18:58 | `1050ti-index-data-20261110-185841` | La shadow CPU dell'IB conteneva `[0,1,2,2,1,3]`, ma il readback D3D11 dell'IB era `[0,0,0,0,0,0]`. |
| 19:00 | `1050ti-upload-flush-20261110-190026` | Dopo il flush dell'upload prima del bind IA, CPU e D3D11 leggevano entrambi `[0,1,2,2,1,3]`. |
| 19:02 | `1050ti-magenta-ps-20261110` | Il readback sorgente e backbuffer e' magenta uniforme: 1.044.480 pixel `RGB(255,0,255)`. |
| 19:04 | `1050ti-vertex-color-20261110` | Readback: 1.040.400 pixel bianchi e 4.080 pixel neri, risoluzione 1360x768. |
| 19:05 | `1050ti-texture-sample-20261110` | Il test PS che campiona t0 ha prodotto un readback interamente nero. Il contenuto della texture non era ancora stato letto direttamente. |
| 19:21–19:27 | `1050ti-descriptor-write-diag-20261110-192115`, `1050ti-descriptor-copy-diag-20261110-192657` | Prima della correzione, t0 risultava non bindata (`srv_mask=0`); il trace di `CopyDescriptors` mostrava `pSrcDescriptorRangeSizes == nullptr`. |
| 19:30 | `1050ti-copydescriptors-fix-20261110-193019` | Dopo il fix che tratta le size nulle come una copia per range, t0 e s0 sono presenti: `srv_mask=0x0001`; t0 e' una texture 1x1, SRV `R8G8B8A8_UNORM` (28), risorsa `R8G8B8A8_TYPELESS` (27). Readback: 1.040.400 pixel neri e 4.080 bianchi. |
| 19:33 | `1050ti-converted-ps-test-20261110-193600` | VS `20e0826b236597a1` (1768 byte) e PS `44b0a992c2304254` (1560 byte, `ps_5_0`) caricati. Il target draw usa indici corretti; statistiche: 4 VS, 2 primitive clip, 1.044.480 PS invocations. Sorgente e backbuffer sono neri uniformi. |
| 19:47 | `1050ti-cbv-readback-20261110-1942` | La sonda conferma che i primi 16 byte dei root-CBV in shadow CPU coincidono con quelli dei CB effettivamente bindati in D3D11 per VS b0 e PS b0. I dettagli sono in `cbv-readback-extract.txt`. |

Gli estratti e le anteprime sono organizzati per run:

- [IB readback iniziale](1050ti-index-data-20261110-185841/README.md)
- [Flush upload IB/VB](1050ti-upload-flush-20261110-190026/README.md)
- [Diagnostica draw target](1050ti-target-draw-diag-20261110/README.md)
- [Statistiche pipeline](1050ti-pipeline-stats-20261110/README.md)
- [Readback IB preliminare](1050ti-index-readback-20261110-185743/README.md)
- [Pixel shader magenta](1050ti-magenta-ps-20261110/README.md)
- [Pixel shader vertex color](1050ti-vertex-color-20261110/README.md)
- [Pixel shader texture sample](1050ti-texture-sample-20261110/README.md)
- [Diagnostica PS binding](1050ti-ps-binding-diag-20261110-190829/README.md)
- [Diagnostica root binding](1050ti-root-bind-diag-20261110-191540/README.md)
- [Diagnostica root descriptor](1050ti-root-descriptor-late-diag-20261110-192426/README.md)
- [Diagnostica descriptor write](1050ti-descriptor-write-diag-20261110-192115/README.md)
- [Diagnostica CopyDescriptors](1050ti-descriptor-copy-diag-20261110-192657/README.md)
- [Fix CopyDescriptors](1050ti-copydescriptors-fix-20261110-193019/README.md)
- [PS reale convertito](1050ti-converted-ps-test-20261110-193600/README.md)
- [Readback root-CBV](1050ti-cbv-readback-20261110-1942/README.md)

## Readback BMP

Le anteprime PNG incluse sono conversioni lossless dei readback BMP 24-bit
1360x768. Per ciascuno dei run magenta, vertex-color, texture-sample e PS
reale, l'hash del BMP sorgente coincide con quello del BMP backbuffer.
Sono inclusi quattro risultati pixel distinti: magenta, vertex-color,
il readback del fix descrittori e nero (condiviso da texture-sample e PS
reale).

I log raw conservati sono nei rispettivi run `converted-ps` e `cbv-readback`.
Gli estratti essenziali e gli esiti BMP sono in `diagnostic-extracts.txt`;
i valori b0 sono in `1050ti-cbv-readback-20261110-1942/cbv-readback-extract.txt`.

## Diagnosi raggiunta e dato mancante

- I problemi di upload IB/VB e di copia descrittori sono stati riprodotti
  e misurati; dopo i fix gli indici sono corretti e SRV/sampler risultano
  bindati.
- Il root-CBV non e' la spiegazione del frame nero: i primi 16 byte della
  shadow CPU e del buffer D3D11 coincidono.
- Nel PS reale, `SV_Target.w` e' calcolato come
  `_15_m0[0].z * ((_66 * -2) + 1) + _66`; con `.z == 0`, l'alpha risultante
  e' `_66`, non zero per definizione.
- Al primo draw, PS b0 e' `(1, 0.454545438, 0, 1)`; il draw successivo
  registra `(2.2, 1, 0, 1)`. La formula HLSL esclude l'ipotesi semplicistica
  che `.z == 0` implichi alpha zero.
- Il dato discriminante ancora mancante e' il contenuto effettivo del texel
  1x1 campionato da t0, acquisito al draw bersaglio. Non e' stata applicata
  alcuna modifica funzionale basata su supposizioni.

Il log completo del run CBV rimaneva nel log globale locale del gioco;
`cbv-readback-extract.txt` conserva le righe pertinenti, incluso l'header e
i marker target. Il profilo sintetico del run e' `run-profile.txt`.

## Confronto con GTX 950M — evidenze aggiornate all'11 novembre

I test controllati dimostrano su entrambe le GPU che il percorso di output
puo' scrivere pixel: sulla 950M i run v004/v008 producono rispettivamente
magenta e bianco a pieno frame; sulla 1050 Ti il run magenta e' a pieno frame
e il test vertex-color produce una forma bianca. Questi sono controlli
diagnostici, non rendering reale di Hela.

| Aspetto | GTX 950M | GTX 1050 Ti |
|---|---|---|
| Pixel shader di gioco convertito | I run v005-v007 restano neri; v010 misura `COLOR.r=1` ma `t0.r=0` nello shader, nonostante il readback staging del t0 1x1 sia bianco. I test `Load`/`SampleLevel` mostrano risultati iniziali neri e variazioni dipendenti dal tempo; il campione tardivo di v011 e' limitato. | Il PS reale e' caricato ed eseguito per 1.044.480 invocazioni, ma source e backbuffer sono neri. I primi 16 byte di VS/PS b0 coincidono tra shadow CPU e CB D3D11; il texel osservato dallo shader non e' ancora misurato. |
| Diagnostica senza override | v014/v015 sono neri; v015 registra 66 draw saltati sul RTV bersaglio, 947 VS skip nell'heartbeat finale e zero shader pronti (placeholder). | Non e' stato raccolto un run baseline equivalente senza override nel set del 10 novembre. |
| Problemi corretti/misurati | I run gestiti hanno ripristinato e attestato DLL/override; gli errori SRV/OOM precedenti non si riproducono in v015, senza provarne la causa. | Il flush upload corregge l'IB; `CopyDescriptors` con size-array null ora copia un descriptor per range e `t0/s0` risultano bindati. |

### Interpretazione comune e limiti

La capacita' di scrivere nel target e nel backbuffer e' verificata su entrambe
le GPU, quindi il nero del gioco non e' spiegato da un'incapacita' generale
della GPU/proxy di produrre output. La 950M fornisce un indizio piu' specifico
sul percorso di sampling: il valore di `t0` osservato dal PS differisce dal
readback staging. Sulla 1050 Ti il descriptor e il sampler sono presenti, ma
non e' stato misurato il valore campionato; il problema non va quindi
considerato identico o gia' localizzato tra le due GPU.

Il prossimo confronto utile e' acquisire, per lo stesso draw e Present,
`t0.Load`/`SampleLevel` effettivo, coordinate UV e valore colore dal pixel
shader, oltre a formato/risorsa e readback staging dello stesso SRV. Prima di
eseguire un nuovo run, specificare sempre l'output GTX: il `ProxyManager.ps1`
versionato ha come `RunRoot` predefinito `evidence\GTX_950M\runs`, che
rischia di archiviare nel percorso errato i test fatti su questa 1050 Ti.

### Allineamento sorgenti C++

Il proxy-source aggiornato include le sonde GTX 950M per vertex buffer, input
layout, texture e fingerprint insieme alle sonde 1050 Ti per root/table,
descriptor e CBV. La cartella attiva e' stata riallineata a questo snapshot
dopo averne archiviato i sorgenti precedenti in uno ZIP; il merge conserva
anche il flush upload IB/VB e il fix `CopyDescriptors` verificati sulla 1050
Ti. La nuova regressione `d3d11_structured_buffer_copy_smoke` e' inclusa nel
CMake aggiornato.

Il `ProxyManager.ps1` richiede ora un `-RunRoot` esplicito per `Prepare` e
`Run`, evitando che un test su questa 1050 Ti finisca sotto `GTX_950M`.

### Follow-up 12 novembre 2026

La build aggiornata e i test manager sono stati verificati; i due tentativi live
del 12 novembre sono terminati prima del primo Present. Evidenze e diagnosi
sono nel [resoconto del 12 novembre](../20261112/README.md).
