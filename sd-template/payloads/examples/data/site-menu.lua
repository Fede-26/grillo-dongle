--- @name Site menu
--- @desc Reads /data/sites.csv, asks which site, then types its host name. Focus a text field first.
--- @tags data, menu
--- @layout it
--- @os windows

-- CSV parsing is up to the script: fs.lines yields one line at a time
-- (without the newline, up to 255 characters; a longer line is cut).
local sites = {}
local first = true
for line in fs.lines("/data/sites.csv") do
    if first then first = false             -- skip the header row
    elseif line ~= "" then
        local name, host = line:match("^([^,]*),(.*)$")
        sites[#sites + 1] = {name = name, host = host}
    end
end
if #sites == 0 then error("no sites in /data/sites.csv") end

local names = {}
for i, s in ipairs(sites) do names[i] = s.name end

local i = ui.menu("Which site?", names)     -- nil is impossible: abort raises an error
local site = sites[i]
ui.status(site.name)
kb.type("ping ${host}\n", {host = site.host})
