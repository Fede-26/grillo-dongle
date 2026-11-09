--- @name Type and tap
--- @desc kb.type with ${variables}, kb.tap for single keys. Focus a text editor first.
--- @tags basics, text
--- @layout it
--- @os windows

-- kb.type writes a UTF-8 string with the layout and OS in the header.
-- "\n" presses Enter, "\t" presses Tab.
kb.type("Plain text, accents: àèéìòù, symbols: @ # € { } [ ] \\ |\n")

-- ${name} is replaced from a table passed as second argument...
kb.type("Hello ${who}, today is ${day}.\n", {who = "Mario", day = "Monday"})

-- ...or, without a table, from a GLOBAL variable. A "local" is not visible
-- to kb.type, and an undefined name is an error rather than typed literally.
site = "Roma"
kb.type("Site: ${site}\n")

-- kb.tap presses and releases one key. Names: ENTER, TAB, ESC, BACKSPACE,
-- SPACE, DELETE, HOME, END, PAGEUP, PAGEDOWN, UP, DOWN, LEFT, RIGHT, F1..F24.
kb.tap("TAB", 2)          -- twice
kb.tap("a")               -- a single character of the current layout
kb.tap("ENTER")
