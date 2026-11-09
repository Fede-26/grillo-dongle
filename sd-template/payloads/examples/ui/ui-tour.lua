--- @name UI tour
--- @desc status, progress, menu, confirm (with timeout) and wait_button. Types nothing.
--- @tags ui, demo

-- ui.status(text): one line below the title, replaces the previous one.
ui.status("Hello from Lua")
sys.sleep(1000)

-- ui.progress(value [, max=100]): bar under the status line.
for i = 1, 20 do
    ui.status("Working " .. i .. "/20")
    ui.progress(i, 20)
    sys.sleep(100)
end

-- ui.menu(title, items): click = down, double = up, long = choose.
-- Returns the 1-based index. Holding 3 s aborts the script.
local choice = ui.menu("Pick one", {"Alpha", "Beta", "Gamma"})
ui.status("You chose " .. ({"Alpha", "Beta", "Gamma"})[choice])
sys.sleep(1200)

-- ui.confirm(text [, default_yes [, timeout_ms]]) -> boolean.
-- With a timeout, no gesture means the default answer (here: no) after 8 s.
-- A click means somebody is there, so the timeout stops. Good with autorun.
local go = ui.confirm("Continue?", false, 8000)
ui.status(go and "Confirmed" or "Declined")
sys.sleep(1200)

-- ui.wait_button() -> "click" | "double" | "long".
ui.status("Press the button")
local g = ui.wait_button()
ui.status("Gesture: " .. g)
sys.sleep(1200)
