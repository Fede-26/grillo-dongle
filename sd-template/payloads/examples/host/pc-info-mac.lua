--- @name PC info (macOS)
--- @desc Types shell commands that read date, macOS version, model, serial, CPU and RAM of the Mac; asks for an optional comment. Terminal focused.
--- @tags host, info, mac
--- @layout it
--- @os mac
--- @start_delay 2000

local host = require("host")
-- Everything is typed into the focused window: wait until the user says it is a terminal
require("common").pause("Focus a terminal, then click")
host.shell("sh")              -- types `exec sh` first, then sets up the channel

-- {label, expression[, text when empty]}
local steps = {
    {"date",   [[date '+%Y-%m-%d %H:%M:%S']]},
    {"os",     [[sw_vers -productName; sw_vers -productVersion]]},
    -- "MacBook Air" rather than the identifier "Mac14,2" (the comma would become "_")
    {"model",  [[system_profiler SPHardwareDataType | awk -F': ' '/Model Name/{print $2}']]},
    {"serial", [[ioreg -rd1 -c IOPlatformExpertDevice | awk -F'"' '/IOPlatformSerialNumber/{print $4}']]},
    {"cpu",    [[sysctl -n machdep.cpu.brand_string | sed -e 's/(R)//g' -e 's/(TM)//g']]},
    {"ram_gb", [[sysctl -n hw.memsize | awk '{printf "%.1f", $1 / 1073741824}']]},
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
