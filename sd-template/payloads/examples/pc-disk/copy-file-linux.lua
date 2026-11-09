--- @name Copy file (Linux)
--- @desc Copies /data/files/setup.sh from the SD to /tmp on a Linux shell. Automount, or sudo mount if configured.
--- @tags pc-disk, linux
--- @layout it
--- @os linux
--- @start_delay 1500

-- Needs a terminal with focus. By default pcdisk waits for the desktop automount
-- (udisks: /media/<user>/... or /run/media/<user>/...). Without automount, mount by hand
-- with sudo: set `pcdisk = { mount = "sudo" }` in config.lua, or pass {mount = "sudo"}.
-- The sudo password comes from `secrets = { sudo = "..." }` in config.lua.
local pcdisk = require("pcdisk")

local sudo = cfg("pcdisk.mount", "auto") == "sudo"
pcdisk.run([[cp "$d/data/files/setup.sh" /tmp/ && chmod +x /tmp/setup.sh]], {mount = sudo and "sudo" or "auto"})

ui.status("Copied")
sys.sleep(1500)
