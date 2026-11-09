--- @name Linux post-install
--- @desc Updates apt and installs a package list with progress, using the sudo password from config.lua. Terminal focused.
--- @tags practical, linux, host
--- @layout it
--- @os linux
--- @start_delay 2000
--- @timeout 1800

-- config.lua must contain:   secrets = { sudo = "the password" }
-- kb.type_secret types it without the script ever holding it as a string.
local host = require("host")

host.shell("sh")

-- "sudo -k" forgets the cached credentials, so the prompt always appears and the
-- password is never typed into the shell by mistake.
kb.type("sudo -k; sudo -v\n")
sys.sleep(1500)
kb.type_secret("sudo")
kb.tap("ENTER")
sys.sleep(1000)

local PACKAGES = {"curl", "git", "htop", "vim"}
ui.status("apt update")
if host.run([[sudo apt-get update -y]], {timeout = 300000}) ~= 0 then error("apt update failed") end

for i, pkg in ipairs(PACKAGES) do
    ui.progress(i - 1, #PACKAGES)
    ui.status("install " .. pkg)
    local code = host.run([[sudo apt-get install -y ]] .. pkg, {timeout = 600000})
    if code ~= 0 then error(pkg .. ": exit " .. tostring(code)) end
end
ui.progress(#PACKAGES, #PACKAGES)
ui.status("Done")
sys.sleep(1500)
