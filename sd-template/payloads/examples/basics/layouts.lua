--- @name Layout comparison
--- @desc Types the same line with each layout to see what the host layout does to it. Types in a text editor.
--- @tags basics, layout
--- @layout us
--- @os linux

-- The dongle sends key POSITIONS; the host turns them into characters with its
-- own layout. If the dongle layout and the host layout differ, characters change.
-- Run this against a host set to IT and compare the three lines.
local TEST = "az AZ 09 ;:'\" []{} @#~` \\| àèé €"

for _, layout in ipairs({"us", "it", "colemak"}) do
    kb.layout(layout)                    -- sets the layout until the script ends
    kb.type(layout .. ": ")
    -- A character missing from the layout (and without a fallback for this OS)
    -- raises an error: pcall lets the script continue and report it.
    local ok, err = pcall(kb.type, TEST)
    kb.type("\n")
    if not ok then
        ui.status(layout .. ": " .. err)
        sys.sleep(2000)
    end
end

-- kb.layout() and kb.os() without arguments only read the current value.
ui.status("now: " .. kb.layout() .. " / " .. kb.os())
