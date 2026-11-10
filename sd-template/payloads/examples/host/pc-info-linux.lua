--- @name PC info (Linux)
--- @desc Types shell commands that read date, OS, model, serial, CPU and RAM of a Linux PC; asks for an optional comment. Any terminal focused.
--- @tags host, info, linux
--- @layout it
--- @os linux
--- @start_delay 2000

local host = require("host")
-- Everything is typed into the focused window: wait until the user says it is a terminal
require("common").pause("Focus a terminal, then click")
host.shell("sh")              -- types `exec sh` first, then sets up the channel

-- {label, expression[, text when empty]}
local steps = {
    {"date",   [[date '+%Y-%m-%d %H:%M:%S']]},
    {"os",     [[. /etc/os-release; echo "$PRETTY_NAME"]]},
    {"model",  [[cat /sys/class/dmi/id/sys_vendor /sys/class/dmi/id/product_name 2>/dev/null]], "unknown (no DMI)"},
    -- Readable by root only on most distributions
    {"serial", [[cat /sys/class/dmi/id/product_serial 2>/dev/null]], "root only"},
    -- lscpu also names ARM CPUs, which have no "model name" in /proc/cpuinfo
    {"cpu",    [[lscpu | sed -n 's/^Model name: *//p' | head -1 | sed -e 's/(R)//g' -e 's/(TM)//g']]},
    {"ram_gb", [[awk '/^MemTotal/{printf "%.1f", $2 / 1048576}' /proc/meminfo]]},
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
