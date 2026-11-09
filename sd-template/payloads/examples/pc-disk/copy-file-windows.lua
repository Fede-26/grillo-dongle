--- @name Copy file (Windows)
--- @desc Copies /data/files/setup.exe from the SD to C:\Temp through PowerShell, then ejects the SD.
--- @tags pc-disk, windows
--- @layout it
--- @os windows
--- @start_delay 1500

-- Put the file on the SD as /data/files/setup.exe (the PC sees it as <drive>:\data\files).
-- pcdisk (lib/pcdisk.lua) does the repetitive part: attaches the SD as a read-only
-- disk, finds its drive letter through the .grillo marker file, runs YOUR line,
-- ejects the disk and waits for the eject.

ui.status("Opening PowerShell")
kb.press("GUI", "r")
sys.sleep(600)
kb.type("powershell\n")
sys.sleep(1500)

local pcdisk = require("pcdisk")
-- $d is the SD root without the trailing slash ("E:"). One line: use ';' between commands.
-- Do not write "${" in the body: kb.type would read it as a script variable.
local how = pcdisk.run([[New-Item -ItemType Directory -Force C:\Temp | Out-Null; Copy-Item "$d\data\files\setup.exe" C:\Temp -Force]])

ui.status("Copied (" .. how .. ")")   -- "eject" if the PC ejected, "click" if you pressed the button
sys.sleep(1500)
