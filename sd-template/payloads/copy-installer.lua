--- @name Copy installer
--- @desc Template: copies a file from the SD to the PC through a PowerShell line, then ejects.
--- @layout it
--- @os windows
--- @start_delay 1000

-- TODO: set layout and os in the header to match the target PC.

ui.status("Opening PowerShell")
kb.press("GUI", "r")
sys.sleep(500)
kb.type("powershell\n")
sys.sleep(1500)

-- pcdisk finds the SD on the PC, runs the body (one line, $d = SD root) and ejects it.
local pcdisk = require("pcdisk")
-- TODO: replace with the real copy command.
pcdisk.run([[Copy-Item "$d\data\installer.exe" "$env:TEMP" -Force]])

ui.status("Installing")
-- TODO: command that runs the installer (kb.type).

ui.status("Done")
