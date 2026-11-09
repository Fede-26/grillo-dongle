--- @name Clone check (Windows)
--- @desc Logs every identifier that could tell cloned PCs apart. Run it on two PCs and compare the CSVs.
--- @tags host, inventory, windows
--- @layout it
--- @os windows
--- @start_delay 2000

-- Identifiers that live in the DISK are copied by Clonezilla (MachineGuid, host name);
-- those in the hardware/firmware should differ, unless the maker left a placeholder
-- like "Default string" or "To be filled by O.E.M.". Run this on two clones and look at
-- which rows differ in /logs/host/*.csv: pick that one as your machine id.
local host = require("host")
host.shell("powershell")
host.machine([[$env:COMPUTERNAME]])   -- hostnames can collide: then use host.machine() alone

local rows = {
    {"mac_all",    [[(Get-NetAdapter -Physical | Sort-Object MacAddress).MacAddress -join ',']]},
    {"bios_serial", [[(Get-CimInstance Win32_BIOS).SerialNumber]]},
    {"board_serial", [[(Get-CimInstance Win32_BaseBoard).SerialNumber]]},
    {"uuid",       [[(Get-CimInstance Win32_ComputerSystemProduct).UUID]]},
    {"machineguid", [[(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Cryptography').MachineGuid]]},
    {"disk_serial", [[(Get-PhysicalDisk | Select-Object -First 1).SerialNumber]]},
    {"hostname",   [[$env:COMPUTERNAME]]},
}
for i, r in ipairs(rows) do
    ui.progress(i, #rows)
    ui.status(r[1])
    host.log(r[1], r[2])
end
ui.status("Done: compare the CSVs")
sys.sleep(2000)
