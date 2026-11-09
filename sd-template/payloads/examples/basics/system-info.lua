--- @name System info
--- @desc Shows serial number, layout, OS and config values on the LCD. Types nothing.
--- @tags basics, info

-- sys.wait_host([ms]) waits until the host has mounted the keyboard.
-- The injector already does it before starting; use it after a disk.attach()
-- or to give a clear message when nothing is connected.
if not sys.wait_host(3000) then
    ui.status("no host connected")
    sys.sleep(2000)
    return
end

local lines = {
    "serial " .. sys.serial(),                      -- USB serial = the dongle's MAC
    "layout " .. kb.layout() .. " / " .. kb.os(),
    -- cfg(key, default) reads config.lua values as strings
    "key_delay " .. cfg("keyboard.key_delay", "8"),
    "rotation " .. cfg("ui.rotation", "0"),
}
for _, l in ipairs(lines) do
    ui.status(l)
    sys.log(l)               -- also in the log file (/logs/grillo.log on the SD)
    sys.sleep(1500)
end
