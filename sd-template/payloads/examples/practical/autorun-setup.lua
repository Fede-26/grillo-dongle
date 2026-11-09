--- @name Autorun setup
--- @desc Meant for autorun: asks for confirmation with a countdown, then types a short setup. Types into a text field.
--- @tags practical, autorun
--- @layout it
--- @os windows

-- In config.lua:   autorun = { app = "injector", payload = "examples/practical/autorun-setup.lua", delay = 3 }
-- The autorun countdown (3 s) can be cancelled with a click. This script adds a second,
-- longer question that proceeds on its own when nobody answers.
if not ui.confirm("Start the setup?", true, 20000) then
    ui.status("Cancelled")
    sys.sleep(1000)
    return
end

led.effect("pulse", 0x30B0F0, 800)
kb.type("setup started from the dongle\n")
led.effect("solid", 0x00FF00)
sys.sleep(1500)
