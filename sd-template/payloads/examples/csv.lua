--- @name CSV lines
--- @desc Reads /data/sites.csv and shows each row on the LCD. Types nothing.

local first = true
for line in fs.lines("/data/sites.csv") do
    if first then first = false else
        ui.status(line)
        sys.sleep(800)
    end
end
