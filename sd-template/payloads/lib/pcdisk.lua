-- Helper that lets the PC use the SD: attaches it as a disk, types in the terminal
-- what is needed to find it, runs the script's commands and ejects it.
--
--   local pcdisk = require("pcdisk")
--   pcdisk.run([[robocopy "$d\data\app" "C:\App" /E]])
--
-- The body is text for the terminal open on the PC (PowerShell on Windows, POSIX
-- shell on Linux and macOS, chosen by kb.os()) and must fit on one line: use `;`.
-- pcdisk.run starts the shell itself (common.shell), so the terminal can have
-- any shell open.
-- Inside the body `$d` is the SD root without a trailing slash ("E:" on Windows,
-- "/Volumes/GRILLO-SD" on macOS). Do not write `${` in the body: kb.type reads it
-- as a script variable.
--
-- On Linux without automount: `pcdisk.run(body, {mount = "sudo"})` (or
-- `pcdisk = { mount = "sudo" }` in config.lua) mounts the SD manually on /mnt/grillo,
-- read-only, with the sudo password taken from `secrets.sudo` in config.lua
-- (name changeable with opts.secret). Before the mount the `.grillo` file cannot be
-- read, so the volume is recognized by the GRILLO-SD label.
--
-- The volume is recognized by the `.grillo` file in its root, not by the label.
-- Everything goes on one line because commands typed while another runs queue up
-- in the terminal buffer and the dongle does not know when they finish.
local M = {}
local common = require("common")

-- For each OS: `find` sets $d (and, on Windows, $r), `eject` ejects.
local SHELLS = {
    windows = {
        find = [[$r=$null; while(-not $r){Start-Sleep 1; $r=(Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path ($_.Root+'.grillo') } | Select-Object -First 1).Root}; $d=$r.TrimEnd('\')]],
        eject = [[(New-Object -ComObject Shell.Application).Namespace(17).ParseName($r).InvokeVerb('Eject')]],
    },
    -- Linux: udisks automount paths (not verified on a real distribution)
    linux = {
        find = [[d=; while [ -z "$d" ]; do sleep 1; f=$(ls /media/*/*/.grillo /run/media/*/*/.grillo 2>/dev/null | head -1); [ -n "$f" ] && d=$(dirname "$f"); done]],
        eject = [[eject "$d"]],
    },
    mac = {
        find = [[d=; while [ -z "$d" ]; do sleep 1; f=$(ls /Volumes/*/.grillo 2>/dev/null | head -1); [ -n "$f" ] && d=$(dirname "$f"); done]],
        eject = [[diskutil eject "$d"]],
    },
}

-- Linux with manual mount: the device is found by its label, mounted and unmounted with sudo.
local SUDO = {
    find = [[dev=; while [ -z "$dev" ]; do sleep 1; dev=$(lsblk -nrpo PATH,LABEL | awk '$2=="GRILLO-SD"{print $1}'); done; d=/mnt/grillo; sudo mkdir -p $d; sudo mount -o ro $dev $d]],
    eject = [[sudo umount $d; sudo eject $dev]],
}

-- pcdisk.run(body [, options]) -> "eject" | "click"
-- options.timeout: ms to wait for the eject (default 180000).
-- options.mount: "auto" (default, or `pcdisk.mount` in config.lua) or "sudo" (Linux only).
-- options.secret: name in `secrets` of the sudo password (default "sudo").
-- Error if the SD is not ejected in time.
function M.run(body, opts)
    opts = opts or {}
    local mode = opts.mount or cfg("pcdisk.mount", "auto")
    local sh = SHELLS[kb.os()]
    local timeout = opts.timeout or 180000

    if mode == "sudo" then
        if kb.os() ~= "linux" then error("pcdisk: mount = sudo works only with kb.os() = linux") end
        sh = SUDO
    elseif mode ~= "auto" then
        error("pcdisk: mount must be auto or sudo")
    end

    ui.status("Starting the shell")
    common.shell(kb.os() == "windows" and "powershell" or "sh")

    ui.status("Attaching the SD")
    disk.attach()

    if mode == "sudo" then
        -- sudo -k clears the cache: without it the prompt might not appear and the
        -- password would be typed into the shell, in plain text and into the history.
        ui.status("sudo")
        kb.type("sudo -k; sudo -v\n")
        sys.sleep(1500)
        kb.type_secret(opts.secret or "sudo")
        kb.tap("ENTER")
        sys.sleep(1000)
    end

    ui.status("Running on the PC")
    kb.type(table.concat({sh.find, body, sh.eject}, "; ") .. "\n")

    ui.status("Waiting for eject")
    local how = disk.wait_eject(timeout)
    if not how then error("SD not ejected within " .. (timeout // 1000) .. " s") end
    disk.detach()
    return how
end

return M
