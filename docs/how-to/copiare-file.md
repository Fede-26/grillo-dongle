# Copiare file sul PC con il disco USB

Uno script può far apparire la microSD come disco sul PC (in sola lettura), digitare i comandi che copiano i file, e poi scollegarla. Il PC deve avere una shell raggiungibile da tastiera.

Usa la libreria `pcdisk` (`sd-template/payloads/lib/pcdisk.lua`, da copiare in `/payloads/lib` sulla SD). Fa lei i passi ripetitivi: collega il disco, trova l'unità sul PC dal file `.grillo`, esegue il tuo comando, espelle, attende l'espulsione e scollega.

```lua
--- @name Copia installer
--- @layout it
--- @os windows

ui.status("Apro la shell")
kb.press("GUI", "r")
sys.sleep(500)
kb.type("powershell\n")
sys.sleep(1500)

local pcdisk = require("pcdisk")
pcdisk.run([[Copy-Item "$d\data\setup.exe" C:\Temp -Force]])
```

`$d` è la radice della SD senza barra finale (`E:`). Il corpo è una sola riga: concatena più comandi con `;`, perché i comandi digitati mentre un altro è in corso si accodano e il dongle non sa quando finiscono. Un esempio è in `sd-template/payloads/copy-installer.lua`.

Senza la libreria si possono usare direttamente `disk.attach`, `disk.wait_eject` e `disk.detach` ([API Lua](../reference/lua-api.md#disk-la-microsd-come-disco-sul-pc)).

## Cose da sapere

- Mentre il disco è collegato il mouse non funziona (`mouse.*` dà errore) e il firmware non scrive sulla scheda: i log sono scaricati prima.
- `disk.wait_eject` termina anche con un click sul pulsante, utile se l'espulsione non è rilevata.
- I comandi per copiare ed espellere dipendono dal PC: il dongle non li conosce, li digiti tu con `kb.type`.
- `pcdisk.run` avvia per prima cosa la shell giusta nel terminale in primo piano (`powershell -NoProfile` su Windows, `exec sh` su Linux e macOS), quindi il corpo funziona anche se è aperto `cmd.exe` o fish. Aprire il terminale resta compito dello script, come nell'esempio sopra.
- Se lo script finisce o si interrompe con il disco ancora collegato, il dongle lo scollega.
