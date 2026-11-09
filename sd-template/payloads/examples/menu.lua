--- @name Menu and progress
--- @desc Asks which site to configure, then shows a progress bar. Types nothing.

local sites = {"Site A", "Site B", "Site C"}
local i = ui.menu("Which site?", sites)
ui.status("Preparing " .. sites[i])
for step = 1, 10 do
    ui.progress(step, 10)
    sys.sleep(200)
end
ui.status("Done")
