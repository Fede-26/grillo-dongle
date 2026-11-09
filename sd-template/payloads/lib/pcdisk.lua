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

-- pcdisk.run(corpo [, opzioni]) -> "eject" | "click"
-- opzioni.timeout: ms di attesa dell'espulsione (default 180000).
-- Errore se la SD non viene espulsa in tempo.
function M.run(body, opts)
    opts = opts or {}
    local sh = SHELLS[kb.os()]
    local timeout = opts.timeout or 180000

    ui.status("Collego la SD")
    disk.attach()

    ui.status("Eseguo sul PC")
    kb.type(table.concat({sh.find, body, sh.eject}, "; ") .. "\n")

    ui.status("Attendo l'espulsione")
    local how = disk.wait_eject(timeout)
    if not how then error("SD non espulsa entro " .. (timeout // 1000) .. " s") end
    disk.detach()
    return how
end

return M
