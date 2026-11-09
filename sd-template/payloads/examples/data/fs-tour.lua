--- @name Files tour
--- @desc Lists, checks and reads files under /payloads and /data (read-only). Types nothing.
--- @tags data, fs

-- Only /payloads and /data are readable, and ".." is refused.
for _, dir in ipairs({"/payloads", "/data"}) do
    local names = fs.list(dir)               -- nil, "cannot open" when missing
    ui.status(dir .. ": " .. (names and #names or 0) .. " items")
    sys.sleep(1000)
end

if fs.exists("/data/sites.csv") then
    -- fs.read returns the whole file (up to 16 KB, bigger is an error)
    local text = fs.read("/data/sites.csv")
    ui.status(#text .. " bytes")
    sys.log(text)
else
    ui.status("no sites.csv")
end
sys.sleep(1500)

-- Libraries: require("name") loads /payloads/lib/name.lua once.
local common = require("common")
common.pause("Click to finish")
