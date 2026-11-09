--- @name Shortcuts
--- @tags demo, keys
--- @desc Select all, copy, then paste with the host's shortcut keys. Focus a text field first.
--- @layout us
--- @os mac

-- kb.press sends every key together, then releases. Use CTRL on Windows/Linux.
kb.press("GUI", "a")
kb.press("GUI", "c")
sys.sleep(300)
kb.press("GUI", "v")
