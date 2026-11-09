# Copiare file sul PC con il disco USB

Uno script può far apparire la microSD come disco sul PC (in sola lettura), digitare i comandi che copiano i file, e poi scollegarla. Il PC deve avere una shell raggiungibile da tastiera.

Schema (il modello completo è `sd-template/payloads/examples/copy-installer.lua`, con i comandi da scrivere per il tuo sistema):

```lua
--- @name Copia installer
--- @layout it
--- @os windows

ui.status("Apro la shell")
kb.press("GUI", "r")
sys.sleep(500)
kb.type("powershell\n")
sys.sleep(1500)

ui.status("Collego la SD")
disk.attach()                  -- il dongle si ri-enumera: attende la tastiera

ui.status("Copio")
-- kb.type("comando che copia i file dal volume Grillo\n")

ui.status("Espello")
-- kb.type("comando che espelle il volume\n")

local how = disk.wait_eject(60000)   -- "eject", "click" o nil
if not how then error("SD non espulsa entro 60 s") end

disk.detach()
```

## Cose da sapere

- Mentre il disco è collegato il mouse non funziona (`mouse.*` dà errore) e il firmware non scrive sulla scheda: i log sono scaricati prima.
- `disk.wait_eject` termina anche con un click sul pulsante, utile se l'espulsione non è rilevata.
- I comandi per copiare ed espellere dipendono dal PC: il dongle non li conosce, li digiti tu con `kb.type`.
- Se lo script finisce o si interrompe con il disco ancora collegato, il dongle lo scollega.
