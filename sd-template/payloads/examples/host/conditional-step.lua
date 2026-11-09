--- @name Conditional step
--- @desc Reads a value from the PC and decides what to do: skips a step already done. PowerShell focused.
--- @tags host, windows, practical
--- @layout it
--- @os windows
--- @start_delay 2000

local host = require("host")
host.shell("powershell")

-- host.get returns text: Test-Path gives "True" / "False".
local installed = host.get([[Test-Path 'C:\Program Files (x86)\AnyDesk\AnyDesk.exe']])
if installed == "True" then
    ui.status("AnyDesk already there")
    sys.sleep(1500)
    return
end

if not ui.confirm("Install AnyDesk?", true, 15000) then return end   -- 15 s, default yes

-- Use host.run for the step and check the exit code.
local code = host.run([[winget install --id AnyDeskSoftware.AnyDesk --silent --accept-package-agreements --accept-source-agreements]],
    {timeout = 600000})
if code == nil then error("install did not finish in 10 min") end
if code ~= 0 then error("winget exit code " .. code) end
ui.status("Installed")
sys.sleep(1500)
