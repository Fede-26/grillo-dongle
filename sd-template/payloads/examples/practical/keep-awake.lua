--- @name Keep awake
--- @desc Moves the mouse a tiny bit every 30 s so the PC does not lock. Hold the button 3 s to stop.
--- @tags practical, mouse
--- @timeout 3600

-- @timeout (seconds) makes the script end by itself after one hour.
local n = 0
while true do
    mouse.move(1, 0); sys.sleep(50); mouse.move(-1, 0)
    n = n + 1
    ui.status("pulses: " .. n)
    sys.sleep(30000)      -- sys.sleep is interruptible: hold 3 s aborts it
end
