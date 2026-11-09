--- @name Install from SD (Windows)
--- @desc Copies a folder and certificates from the SD, runs an installer, checks the result. Admin PowerShell focused.
--- @tags practical, windows, pc-disk, host
--- @layout it
--- @os windows
--- @start_delay 2000
--- @timeout 1800

-- Expected on the SD:
--   /data/app/                 -> copied to C:\App
--   /data/app/setup.exe        -> run silently (/S) after the copy
--   /data/certs/Root/*.cer     -> imported in Cert:\LocalMachine\Root
-- (a sub-folder per certificate store: Root, CA, TrustedPublisher, ...; no .pfx)
local host = require("host")
local pcdisk = require("pcdisk")

if not fs.exists("/data/app") then error("missing /data/app on the SD") end
host.shell("powershell")

-- 1) Copy, import certificates, eject: ONE line, because the dongle cannot know
-- when typed commands finish. robocopy exit codes below 8 mean success.
local DST = [[C:\App]]
pcdisk.run(table.concat({
    [[$s="$d\data"]],
    [[robocopy "$s\app" "]] .. DST .. [[" /E /NFL /NDL /NJH /NJS]],
    [[Get-ChildItem "$s\certs" -Directory | ForEach-Object { $c=$_.Name; Get-ChildItem $_.FullName -File | ForEach-Object { Import-Certificate -FilePath $_.FullName -CertStoreLocation "Cert:\LocalMachine\$c" | Out-Null } }]],
}, "; "))

-- 2) With the SD ejected, host.run gives a real exit code per command.
ui.status("Installing")
local code = host.run([[Start-Process -Wait -FilePath ']] .. DST .. [[\setup.exe' -ArgumentList '/S']], {timeout = 900000})
if code ~= 0 then error("installer failed: " .. tostring(code)) end

-- 3) Verify with host.get and log the outcome in this machine's CSV.
host.machine()
local present = host.get([[Test-Path ']] .. DST .. [[\app.exe']])
host.log("install", [['app.exe present: ]] .. present .. [[']])
ui.status("Installed: " .. tostring(present))
sys.sleep(2000)
