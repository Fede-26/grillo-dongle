--- @name Host channel (sh)
--- @desc Same as host-basics for sh/bash/zsh on Linux or macOS. Open a terminal and give it focus.
--- @tags host, linux, mac
--- @layout it
--- @os linux
--- @start_delay 2000

-- Change "@os linux" to "mac" in the header for a Mac.
local host = require("host")

host.shell("sh")           -- types `exec sh` first: any shell works (bash, zsh, fish)

local code = host.run([[sh -c 'exit 5']])
ui.status("exit code: " .. tostring(code))      -- 5
sys.sleep(1500)

code = host.run([[test -d /tmp]])
ui.status("test -d /tmp: " .. tostring(code))   -- 0 when it exists
sys.sleep(1500)

-- Several lines of output are joined with spaces; the value is cut at 256 characters.
local kernel = host.get([[uname -sr]])
ui.status(tostring(kernel))
sys.sleep(2500)
