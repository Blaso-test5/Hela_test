# 1050 Ti — readback root-CBV

La sonda conferma che i primi 16 byte dei root-CBV nella shadow CPU e dei
constant buffer D3D11 effettivamente bindati coincidono per VS b0 e PS b0.
Sono registrati 12 root bind, 24 valori root-CBV, 18 draw diagnostici e 12
letture D3D11 dei CB. Al controllo successivo il processo Hela non era
presente; non e' stata registrata una causa di uscita.

- Valori e log: [`cbv-readback-extract.txt`](cbv-readback-extract.txt)
- Profilo: [`run-profile.txt`](run-profile.txt)
- Log raw: [`d3d12on11.log`](d3d12on11.log)
