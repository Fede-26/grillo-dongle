--- @name Hello
--- @tags demo, text
--- @desc Types a greeting with a template variable. Focus a text field first.
--- @layout colemak
--- @os mac
--- @key_delay 10

kb.type("Hello from ${who}!\n", {who = "Grillo"})
