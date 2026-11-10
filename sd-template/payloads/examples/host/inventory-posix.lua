--- @name Inventory (Linux / macOS)
--- @desc Types shell commands that read model, serial and OS of a Linux or macOS machine, and logs them to /logs/host/<MAC>.csv. Terminal focused.
--- @tags host, inventory, linux, mac
--- @layout it
--- @os linux
--- @start_delay 2000

-- Set "@os mac" for a Mac. The id is the MAC of a physical interface (Linux) or of en0 (macOS).
local host = require("host")
host.shell("sh")
host.machine()

local steps
if kb.os() == "mac" then
    steps = {
        {"model",  [[sysctl -n hw.model]]},
        {"serial", [[ioreg -l | awk -F'"' '/IOPlatformSerialNumber/{print $4}']]},
        {"os",     [[sw_vers -productVersion]]},
        {"hostname", [[hostname]]},
    }
else
    -- /sys/class/dmi/id files are readable by root only on some systems: empty value then
    steps = {
        {"model",  [[cat /sys/class/dmi/id/product_name]]},
        {"serial", [[cat /sys/class/dmi/id/product_serial 2>/dev/null]]},
        {"os",     [[. /etc/os-release; echo $PRETTY_NAME]]},
        {"kernel", [[uname -r]]},
        {"hostname", [[hostname]]},
    }
end
for i, s in ipairs(steps) do
    ui.progress(i, #steps)
    ui.status(s[1] .. ": " .. host.log(s[1], s[2]))
    sys.sleep(700)
end
