--- @name Clock from the PC
--- @desc Sets the dongle clock from the PC with host.clock(), then logs with dates. Open a terminal (PowerShell on Windows) and give it focus.
--- @tags host, clock
--- @layout it
--- @os windows
--- @start_delay 2000

-- host.clock() asks the PC for UTC time over the host channel and sets the
-- dongle clock; the timezone stays the one of config.lua. Useful offline (no
-- WiFi, so no NTP) before writing logs that need a date.
local host = require("host")

-- Same script for every OS: PowerShell on Windows, sh elsewhere.
host.shell(kb.os() == "windows" and "powershell" or "sh")

if sys.time() then
    ui.status("already known: " .. sys.date())
    sys.sleep(1500)
end

local now = host.clock()                 -- error if the PC does not answer
ui.status("PC time: " .. now)
sys.log("clock set from the PC")         -- this line in /logs/grillo.log has the date
sys.sleep(2000)

-- From here on sys.date() works until the dongle loses power.
ui.status("done at " .. sys.date("%H:%M:%S"))
sys.sleep(1500)
