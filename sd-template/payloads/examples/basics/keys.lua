--- @name Navigation keys
--- @desc Moves through a text and edits it with special keys. Focus a text editor with a few lines first.
--- @tags basics, keys
--- @layout it
--- @os windows

kb.type("first line\nsecond line\nthird line")
sys.sleep(300)

kb.press("CTRL", "HOME")        -- start of the document
kb.tap("DOWN")                   -- second line
kb.tap("END")
kb.tap("BACKSPACE", 4)           -- delete "line"
kb.type("row")
kb.press("SHIFT", "HOME")        -- select to the start of the line
kb.press("CTRL", "x")            -- cut
kb.tap("F5")                     -- function key (refreshes in many apps)
