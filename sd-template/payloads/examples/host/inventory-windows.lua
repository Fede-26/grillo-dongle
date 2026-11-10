--- @name Inventory (Windows)
--- @desc Types PowerShell commands that read serial, model, UUID and OS build of the PC, and logs them to /logs/host/<MAC>.csv on the SD. PowerShell focused.
--- @tags host, inventory, windows
--- @layout it
--- @os windows
--- @start_delay 2000

-- One CSV per machine on the SD: /logs/host/<id>.csv, lines "date,script,label,value".
-- The machine id is the MAC address of the first physical network adapter: it belongs
-- to the hardware, so two PCs cloned with Clonezilla still get different files
-- (the Windows MachineGuid and the host name are copied with the disk).
local host = require("host")
host.shell("powershell")

local id = host.machine()                -- also reads the PC's date for the log lines
ui.status("machine " .. id)

-- host.log(label, expr) runs expr on the PC, returns the value and appends it to the CSV.
local steps = {
    {"serial",  [[(Get-CimInstance Win32_BIOS).SerialNumber]]},
    {"model",   [[(Get-CimInstance Win32_ComputerSystem).Model]]},
    {"uuid",    [[(Get-CimInstance Win32_ComputerSystemProduct).UUID]]},
    {"os",      [[(Get-CimInstance Win32_OperatingSystem).Caption]]},
    {"build",   [[(Get-CimInstance Win32_OperatingSystem).BuildNumber]]},
    {"hostname", [[$env:COMPUTERNAME]]},
}
for i, s in ipairs(steps) do
    ui.progress(i, #steps)
    local v = host.log(s[1], s[2])
    ui.status(s[1] .. ": " .. v)
    sys.sleep(700)
end
ui.status("Logged to " .. id .. ".csv")
sys.sleep(1500)
