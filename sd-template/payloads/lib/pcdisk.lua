-- Helper per far usare la SD al PC: la collega come disco, digita nel terminale
-- ciò che serve per trovarla, esegue i comandi dello script e la espelle.
--
--   local pcdisk = require("pcdisk")
--   pcdisk.run([[robocopy "$d\data\app" "C:\App" /E]])
--
-- Il corpo è testo per il terminale aperto sul PC (PowerShell su Windows, shell
-- POSIX su Linux e macOS, scelta da kb.os()) e deve stare in una riga: usa `;`.
-- Dentro il corpo `$d` è la radice della SD senza barra finale ("E:" su Windows,
-- "/Volumes/GRILLO-SD" su macOS). Non scrivere `${` nel corpo: kb.type lo legge
-- come variabile dello script.
--
-- Su Linux senza automount: `pcdisk.run(corpo, {mount = "sudo"})` (oppure
-- `pcdisk = { mount = "sudo" }` in config.lua) monta la SD a mano in /mnt/grillo,
-- in sola lettura, con la password di sudo presa da `secrets.sudo` di config.lua
-- (nome cambiabile con opts.secret). Prima del mount il file `.grillo` non si può
-- leggere, quindi il volume si riconosce dall'etichetta GRILLO-SD.
--
-- Il volume si riconosce dal file `.grillo` nella radice, non dall'etichetta.
-- Tutto va in una riga perché i comandi digitati mentre un altro gira si accodano
-- nel buffer del terminale e il dongle non sa quando finiscono.
local M = {}

-- Per ogni OS: `find` imposta $d (e, su Windows, $r), `eject` espelle.
local SHELLS = {
    windows = {
        find = [[$r=$null; while(-not $r){Start-Sleep 1; $r=(Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path ($_.Root+'.grillo') } | Select-Object -First 1).Root}; $d=$r.TrimEnd('\')]],
        eject = [[(New-Object -ComObject Shell.Application).Namespace(17).ParseName($r).InvokeVerb('Eject')]],
    },
    -- Linux: percorsi di automount di udisks (non verificato su una distribuzione reale)
    linux = {
        find = [[d=; while [ -z "$d" ]; do sleep 1; f=$(ls /media/*/*/.grillo /run/media/*/*/.grillo 2>/dev/null | head -1); [ -n "$f" ] && d=$(dirname "$f"); done]],
        eject = [[eject "$d"]],
    },
    mac = {
        find = [[d=; while [ -z "$d" ]; do sleep 1; f=$(ls /Volumes/*/.grillo 2>/dev/null | head -1); [ -n "$f" ] && d=$(dirname "$f"); done]],
        eject = [[diskutil eject "$d"]],
    },
}

-- Linux con mount a mano: il device si trova dall'etichetta, si monta e si smonta con sudo.
local SUDO = {
    find = [[dev=; while [ -z "$dev" ]; do sleep 1; dev=$(lsblk -nrpo PATH,LABEL | awk '$2=="GRILLO-SD"{print $1}'); done; d=/mnt/grillo; sudo mkdir -p $d; sudo mount -o ro $dev $d]],
    eject = [[sudo umount $d; sudo eject $dev]],
}

-- pcdisk.run(corpo [, opzioni]) -> "eject" | "click"
-- opzioni.timeout: ms di attesa dell'espulsione (default 180000).
-- opzioni.mount: "auto" (default, o `pcdisk.mount` di config.lua) oppure "sudo" (solo Linux).
-- opzioni.secret: nome in `secrets` della password di sudo (default "sudo").
-- Errore se la SD non viene espulsa in tempo.
function M.run(body, opts)
    opts = opts or {}
    local mode = opts.mount or cfg("pcdisk.mount", "auto")
    local sh = SHELLS[kb.os()]
    local timeout = opts.timeout or 180000

    if mode == "sudo" then
        if kb.os() ~= "linux" then error("pcdisk: mount = sudo vale solo con kb.os() = linux") end
        sh = SUDO
    elseif mode ~= "auto" then
        error("pcdisk: mount deve essere auto o sudo")
    end

    ui.status("Collego la SD")
    disk.attach()

    if mode == "sudo" then
        -- sudo -k azzera la cache: senza, il prompt potrebbe non comparire e la
        -- password finirebbe digitata nella shell, in chiaro e nella cronologia.
        ui.status("sudo")
        kb.type("sudo -k; sudo -v\n")
        sys.sleep(1500)
        kb.type_secret(opts.secret or "sudo")
        kb.tap("ENTER")
        sys.sleep(1000)
    end

    ui.status("Eseguo sul PC")
    kb.type(table.concat({sh.find, body, sh.eject}, "; ") .. "\n")

    ui.status("Attendo l'espulsione")
    local how = disk.wait_eject(timeout)
    if not how then error("SD non espulsa entro " .. (timeout // 1000) .. " s") end
    disk.detach()
    return how
end

return M
