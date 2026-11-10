-- Canale di ritorno dal PC: lo script digita un comando nel terminale e sa
-- quando è finito e con quale codice di uscita.
--
--   local host = require("host")
--   host.shell("powershell")            -- una volta, con il terminale in primo piano
--   local code = host.run([[Start-Process notepad -Wait]])
--   if code ~= 0 then error("notepad: " .. tostring(code)) end
--
-- Come funziona: dopo il comando lo script digita una coda che scrive la riga
-- "@G<nonce>:<codice>" sulla porta seriale (CDC) del dongle. Il dongle accetta
-- solo quella riga: dal PC arrivano "finito" e un numero, mai l'output.
-- La porta si trova dal numero di serie USB del dongle (sys.serial()), quindi
-- funziona anche con più dongle collegati.
--
-- Leggere e registrare valori (inventario):
--   local mac = host.machine()                       -- id della macchina: MAC di una scheda fisica
--   host.log("seriale", [[(Get-CimInstance Win32_BIOS).SerialNumber]])
--   local model = host.get([[(Get-CimInstance Win32_ComputerSystem).Model]])
--   host.clock()                                     -- ora del PC sul dongle (log, sys.date)
-- Il valore è al massimo di 256 caratteri; il dongle sostituisce con "_" tutto ciò
-- che non è lettera, cifra, ". _ : -" o spazio. Non ridigitare mai un valore letto
-- dal PC dentro un comando di shell senza controllarlo: può contenere ciò che ha
-- scelto chi ha configurato la macchina.
--
-- Il comando deve essere bloccante: con un programma che si stacca subito
-- (es. `Start-Process` senza `-Wait`, `open` su macOS) il codice arriva appena
-- parte, non quando finisce. Non scrivere `${` nel comando: kb.type lo legge
-- come variabile dello script. Shell supportate: PowerShell, sh/bash/zsh (non
-- cmd.exe: per ora nessuna variante).
local M = {}

local kind -- "powershell" | "sh", impostata da host.shell

local function nonce()
    return string.format("%06x", math.random(0, 0xFFFFFF))
end

-- Testo da digitare una volta per shell: trova la porta e definisce Send-G / $G.
local function helper(serial)
    if kind == "powershell" then
        -- La COM è figlia del dispositivo composito, il cui id contiene il numero di serie.
        return [[$G=(Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' -and (Get-PnpDeviceProperty -InstanceId $_.PNPDeviceID -KeyName DEVPKEY_Device_Parent).Data -like '*]]
            .. serial .. [[*' } | Select-Object -First 1 -ExpandProperty Name) -replace '.*\((COM\d+)\).*','$1'; function Send-G($n,$c){$p=New-Object IO.Ports.SerialPort $G,115200; $p.Open(); $p.WriteLine('@G'+$n+':'+$c); $p.Close()}]]
    end
    -- macOS: /dev/cu.usbmodem<serie><n>; Linux: /dev/serial/by-id/*<serie>*. `sh -c` per
    -- avere i pattern senza corrispondenza passati a ls (zsh darebbe errore).
    return [[G=$(sh -c 'ls /dev/cu.usbmodem*]] .. serial .. [[* /dev/serial/by-id/*]] .. serial
        .. [[* 2>/dev/null' | head -1)]]
end

-- Codice di uscita del comando, o nil se non arriva entro opts.timeout (default 60000 ms).
function M.run(cmd, opts)
    if not kind then error("host.run: chiama prima host.shell()") end
    local n = nonce()
    local line
    if kind == "powershell" then
        line = [[$LASTEXITCODE=0; ]] .. cmd .. [[; Send-G ]] .. n .. [[ $(if($?){0}elseif($LASTEXITCODE){$LASTEXITCODE}else{1})]]
    else
        line = cmd .. [[; c=$?; printf '@G%s:%s\n' ]] .. n .. [[ $c > "$G"]]
    end
    -- expect prima di digitare: la risposta può arrivare mentre il dongle sta ancora premendo tasti
    host.expect(n)
    kb.type(line .. "\n")
    return host.wait((opts and opts.timeout) or 60000)
end

-- Valore (stringa ripulita, max 256 caratteri) dell'espressione `expr` valutata nella
-- shell del PC, o nil se non arriva entro opts.timeout (default 60000 ms).
-- Un'espressione che restituisce più righe è unita con spazi.
function M.get(expr, opts)
    if not kind then error("host.get: chiama prima host.shell()") end
    local n = nonce()
    local line
    if kind == "powershell" then
        line = [[Send-G ]] .. n .. [[ ((]] .. expr .. [[) -join ' ' -replace '\s+',' ')]]
    else
        line = [[printf '@G%s:%s\n' ]] .. n .. [[ "$( (]] .. expr .. [[) | tr '\n' ' ')" > "$G"]]
    end
    host.expect(n)
    kb.type(line .. "\n")
    local _, text = host.wait((opts and opts.timeout) or 60000)
    return text
end

local machine_id, machine_date

local function default_machine_expr()
    if kind == "powershell" then
        return [[(Get-NetAdapter -Physical | Sort-Object MacAddress | Select-Object -First 1).MacAddress]]
    elseif kb.os() == "mac" then
        return [[ifconfig en0 | awk '/ether/{print $2}']]
    end
    return [[for d in /sys/class/net/*/device; do cat "$(dirname "$d")/address"; done | sort | head -1]]
end

-- Imposta l'id della macchina (da cui il nome di /logs/host/<id>.csv) e la data del
-- PC usata nelle righe di log. Senza argomenti usa il MAC della prima scheda di rete
-- fisica (in ordine di indirizzo): è dell'hardware, quindi diverso anche tra due PC
-- clonati con Clonezilla, mentre MachineGuid e nome host si copiano con il disco.
function M.machine(expr)
    machine_id = M.get(expr or default_machine_expr())
    if not machine_id or machine_id == "" then error("host.machine: nessun valore dal PC") end
    machine_date = M.get(kind == "powershell" and [[Get-Date -Format s]] or [[date +%Y-%m-%dT%H:%M:%S]])
    return machine_id
end

-- Imposta l'orologio del dongle con l'ora del PC e restituisce la data locale
-- come sys.date(). Si legge l'ora UTC (secondi dal 1970): il dongle la mostra
-- con il fuso di config.lua (timezone, default Europe/Rome con l'ora legale),
-- quindi un PC con il fuso sbagliato non sposta i log. Il dongle non ha
-- batteria: l'ora resta valida fino allo spegnimento.
function M.clock(opts)
    local v = M.get(kind == "powershell" and "[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()" or "date +%s", opts)
    local t = v and tonumber(v)
    if not t then error("host.clock: nessuna ora dal PC") end
    sys.set_time(t)
    return sys.date()
end

-- Valuta `expr` sul PC come host.get e aggiunge una riga a /logs/host/<id>.csv sulla
-- SD del dongle (con il disco collegato la riga aspetta il detach). Restituisce il valore.
function M.log(label, expr, opts)
    local value = M.get(expr, opts)
    if value == nil then error("host.log: nessuna risposta per '" .. label .. "'") end
    host.append(machine_id or "unknown", machine_date or "", label, value)
    return value
end

-- Attende che la shell abbia eseguito tutto ciò che è stato digitato prima.
function M.sync(opts)
    return M.run(kind == "powershell" and "$null" or ":", opts)
end

-- Prepara la shell ("powershell" o "sh") e verifica il canale: errore se non risponde.
function M.shell(k)
    if k ~= "powershell" and k ~= "sh" then error("host.shell: powershell o sh") end
    kind = k
    local serial = sys.serial()
    if serial == "" then error("host.shell: numero di serie USB non disponibile") end
    kb.type(helper(serial) .. "\n")
    sys.sleep(1500)
    if M.sync({timeout = 15000}) == nil then
        error("host.shell: il PC non risponde sulla seriale del dongle")
    end
end

return M
