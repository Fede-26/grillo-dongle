--- @name Clock
--- @desc Date and time on the dongle: sys.time, sys.date, dated log lines, elapsed time. Types nothing.
--- @tags basics, clock

-- The dongle has no clock battery: after power-up the time is unknown.
-- It becomes known when any app joins a WiFi network as a client (NTP), or when
-- a script reads it from the PC with host.clock() (see examples/host/clock-from-pc.lua).
-- It survives a reboot, not a power loss.

-- sys.time() -> seconds since 1970 (UTC), or nil if the time is unknown.
local t = sys.time()
if not t then
    ui.status("time unknown: join a WiFi network or use host.clock()")
    sys.log("clock: time unknown")
    sys.sleep(3000)
    return
end

-- sys.date([format]) -> local time with the timezone of config.lua
-- (default Europe/Rome, daylight saving time included). Format as in strftime.
ui.status(sys.date())                           -- 2026-11-10 14:03:22
sys.sleep(1500)
ui.status(sys.date("%d/%m/%Y %H:%M %Z"))        -- 10/11/2026 14:03 CET
sys.sleep(1500)

-- With the time known, every line of /logs/grillo.log starts with date and time.
sys.log("clock example started")

-- File-name friendly stamp, e.g. for a value written in a host log.
local stamp = sys.date("%Y%m%d-%H%M%S")
ui.status("stamp " .. stamp)
sys.sleep(1500)

-- Elapsed time in whole seconds.
local start = sys.time()
sys.sleep(2500)
ui.status("elapsed " .. (sys.time() - start) .. " s")
sys.sleep(1500)
