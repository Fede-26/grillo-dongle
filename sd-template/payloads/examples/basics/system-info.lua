--- @name System info
--- @desc Shows the dongle's own USB serial (its MAC, not the PC's), layout, OS and config values on the LCD. Types nothing.
--- @tags basics, info

local lines = {
    -- The dongle's serial, not the PC's: reading PC values needs the host
    -- library, which types commands (see examples/host/pc-info-*.lua)
    "serial " .. sys.serial(),
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
