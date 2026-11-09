--- @name Host channel (PowerShell)
--- @desc host.shell, host.run exit codes, host.sync and host.get. Open a PowerShell window and give it focus.
--- @tags host, windows
--- @layout it
--- @os windows
--- @start_delay 2000

-- The host channel lets the script know when a typed command has finished and how.
-- After each command the dongle types a tail that writes "@G<nonce>:<code>" on its
-- own USB serial port. Only the code (or a short value) comes back, never the output.
local host = require("host")

ui.status("Preparing the shell")
host.shell("powershell")      -- finds the dongle's COM port by its USB serial; error if silent

-- host.run(cmd) -> exit code, or nil on timeout (default 60 s).
-- The command must block until it is done (Start-Process needs -Wait).
local code = host.run([[cmd /c exit 3]])
ui.status("exit code: " .. tostring(code))        -- 3
sys.sleep(1500)

code = host.run([[Get-Date | Out-Null]])           -- success -> 0
ui.status("Get-Date: " .. tostring(code))
sys.sleep(1500)

code = host.run([[Start-Sleep 90]], {timeout = 3000})
ui.status("timeout -> " .. tostring(code))          -- nil
sys.sleep(1500)
host.sync({timeout = 120000})                       -- wait for the Start-Sleep to end too

-- host.get(expr) -> the value as a string, max 256 characters, cleaned by the dongle.
local name = host.get([[$env:COMPUTERNAME]])
ui.status("host: " .. tostring(name))
sys.sleep(2000)

-- Never retype a value read from the PC inside a shell command without checking it.
if name and name:match("^[%w%-]+$") then
    kb.type("echo ok " .. name .. "\n")
end
