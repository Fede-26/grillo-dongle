--- @name PC info (Windows)
--- @desc Types PowerShell commands that read date, OS, model, serial, CPU and RAM of the PC; asks for an optional comment. Any terminal focused.
--- @tags host, info, windows
--- @layout it
--- @os windows
--- @start_delay 2000

local host = require("host")
-- Everything is typed into the focused window: wait until the user says it is a terminal
require("common").pause("Focus a terminal, then click")
host.shell("powershell")      -- types `powershell -NoProfile` first, then sets up the channel

-- {label, expression[, text when empty]}
local steps = {
    {"date",   [[Get-Date -Format 'yyyy-MM-dd HH:mm:ss']]},
    {"os",     [[Get-CimInstance Win32_OperatingSystem | ForEach-Object { $_.Caption + ' ' + $_.Version }]]},
    {"model",  [[Get-CimInstance Win32_ComputerSystem | ForEach-Object { $_.Manufacturer + ' ' + $_.Model }]]},
    {"serial", [[(Get-CimInstance Win32_BIOS).SerialNumber]]},
    -- "(R)" and "(TM)" would come back as "_R_" and "_TM_"
    {"cpu",    [[(Get-CimInstance Win32_Processor | Select-Object -First 1).Name -replace '\((R|TM)\)','']]},
    {"ram_gb", [[[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)]]},
}

-- host.get(expr) runs expr in the PC's shell and returns the text over the
-- dongle's serial port: nothing is read from the screen. The dongle keeps only
-- letters (accented ones too), digits, ". _ : -" and spaces of the value.
for i, s in ipairs(steps) do
    ui.progress(i, #steps)
    local v = host.get(s[2]) or "no answer"
    if v == "" then v = s[3] or "empty" end
    ui.status(s[1] .. ": " .. v)
    sys.log(s[1] .. ": " .. v)  -- the full line, also in /logs/grillo.log on the SD
    sys.sleep(2000)
end
-- Optional comment typed on the PC, e.g. which machine this is ("checkout 2").
-- host.input types a prompt in the terminal and stops: write the text there
-- (no arrow keys), then click the dongle's button.
if ui.confirm("Add a comment?", false, 20000) then
    local note = host.input("Comment")
    if note and note ~= "" then
        ui.status("comment: " .. note)
        sys.log("comment: " .. note)
        sys.sleep(2000)
    end
end
ui.status("Done: also in /logs/grillo.log")
sys.sleep(1500)
