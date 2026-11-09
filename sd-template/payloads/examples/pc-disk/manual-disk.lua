--- @name Manual disk control
--- @desc The disk.* functions without the pcdisk helper: attach, wait for eject or click, detach.
--- @tags pc-disk, advanced
--- @layout it
--- @os windows

-- disk.attach([timeout_ms]): the dongle re-enumerates and the SD appears on the PC
-- as a READ-ONLY disk next to the keyboard (no mouse while attached).
-- Errors: "SD not mounted", or the host does not come back within the timeout.
ui.status("Attaching the SD")
disk.attach(15000)

-- From now on you could type commands with kb.type to use the disk.
-- This example just waits for you to eject it from the PC or to click the button.
ui.status("Eject it or click")
local how = disk.wait_eject(120000)     -- "eject" | "click" | nil (timeout)
if how == nil then
    ui.status("Timeout")
else
    ui.status("Released by " .. how)
end

-- disk.detach(): back to keyboard + mouse only. A script that ends with the disk
-- still attached gets it detached by the firmware anyway.
disk.detach()
sys.sleep(1000)
