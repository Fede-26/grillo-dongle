--- @name Shortcuts by OS
--- @desc Same script, right modifier on Windows/Linux (Ctrl) and macOS (Cmd). Focus a text field first.
--- @tags basics, keys
--- @layout it

-- kb.os() returns "windows", "linux" or "mac": the header or config.lua sets it.
-- Change the header (--- @os mac) or call kb.os("mac") to target another host.
local mod = kb.os() == "mac" and "GUI" or "CTRL"   -- GUI = Cmd on macOS, Win on Windows

kb.type("Select all, copy, paste")
kb.press(mod, "a")          -- every key is pressed together, then released
sys.sleep(200)
kb.press(mod, "c")
sys.sleep(200)
kb.tap("END")
kb.press(mod, "v")

-- Up to 6 keys at once. Letters in a combination mean the key, not Shift+letter.
-- Examples (commented out: they act on the host!):
-- kb.press("CTRL", "ALT", "DELETE")   -- on Windows
-- kb.press("GUI", "r")                -- Win+R
-- kb.press("ALT", "F4")               -- close window
