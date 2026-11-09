-- Shared helpers: require("common") loads /payloads/lib/common.lua once.
local M = {}

-- Waits for a click before a step that needs the user to be ready.
function M.pause(msg)
    ui.status(msg)
    ui.wait_button()
end

return M
