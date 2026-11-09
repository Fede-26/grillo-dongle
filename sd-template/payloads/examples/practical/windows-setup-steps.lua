--- @name Windows setup steps
--- @desc Kiosk hardening as a list of PowerShell steps with progress and exit codes. Admin PowerShell focused.
--- @tags practical, windows, kiosk
--- @layout it
--- @os windows
--- @start_delay 2000
--- @timeout 900

-- Pattern for a long checklist: data (a table of steps) separate from the logic.
-- Each step runs with host.run, so the script knows when it ends and if it worked.
-- A failing step asks what to do instead of silently going on.
local host = require("host")

local STEPS = {
    {"Disable edge swipe",
     [[reg add "HKLM\SOFTWARE\Policies\Microsoft\Windows\EdgeUI" /v AllowEdgeSwipe /t REG_DWORD /d 0 /f]]},
    {"Never sleep (AC)",    [[powercfg /change standby-timeout-ac 0]]},
    {"Screen always on",    [[powercfg /change monitor-timeout-ac 0]]},
    {"Disable hibernate",   [[powercfg /hibernate off]]},
    {"Show file extensions",
     [[Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' HideFileExt 0]]},
}

if not ui.confirm("Apply " .. #STEPS .. " settings?", true, 20000) then return end

host.shell("powershell")
local failed = {}
for i, step in ipairs(STEPS) do
    ui.progress(i - 1, #STEPS)
    ui.status(step[1])
    local code = host.run(step[2], {timeout = 30000})
    if code ~= 0 then
        failed[#failed + 1] = step[1]
        sys.log("step failed: " .. step[1] .. " code=" .. tostring(code))
        if not ui.confirm(step[1] .. " failed (" .. tostring(code) .. "). Go on?", false, 20000) then
            error("stopped at: " .. step[1])
        end
    end
end
ui.progress(#STEPS, #STEPS)
ui.status(#failed == 0 and "All done" or (#failed .. " step(s) failed"))
led.effect("solid", #failed == 0 and 0x00FF00 or 0xFF8800)
sys.sleep(2000)
