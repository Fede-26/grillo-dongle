-- Shared helpers: require("common") loads /payloads/lib/common.lua once.
local M = {}

-- Waits for a click before a step that needs the user to be ready.
function M.pause(msg)
    ui.status(msg)
    ui.wait_button()
end

-- Starts the shell the script's commands are written for, in the focused
-- terminal, once per script: "sh" types `exec sh` (POSIX sh in place of fish,
-- zsh or whatever the user runs), "powershell" types `powershell -NoProfile`
-- (works from cmd.exe too; from PowerShell it is a nested session). No
-- profile and no rc files: the user's prompt, aliases or functions cannot
-- change what the commands do. The host and pcdisk libraries call it; the
-- second call with the same shell does nothing, so a nested PowerShell does
-- not lose what host.shell defined.
local started
function M.shell(kind)
    if started == kind then return end
    if kind == "powershell" then
        kb.type("powershell -NoProfile\n")
        sys.sleep(3000) -- Windows PowerShell can take a couple of seconds to start
    elseif kind == "sh" then
        kb.type("exec sh\n")
        sys.sleep(1000)
    else
        error("common.shell: powershell or sh")
    end
    started = kind
end

return M
