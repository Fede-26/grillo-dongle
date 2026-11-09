--- @name LED effects
--- @desc Cycles through the LED effects. The header also sets a LED effect. Types nothing.
--- @tags ui, led, demo
--- @led pulse 00FFCC

-- Header "@led effect [RRGGBB]" sets the LED while the script runs:
-- off, solid, pulse, blink, rainbow.

led.rgb(255, 0, 0);   ui.status("red");   sys.sleep(800)
led.rgb(0, 255, 0);   ui.status("green"); sys.sleep(800)
led.rgb(0, 0, 255);   ui.status("blue");  sys.sleep(800)

-- led.effect(name [, 0xRRGGBB [, period_ms]])
for _, fx in ipairs({"solid", "pulse", "blink", "rainbow"}) do
    ui.status(fx)
    led.effect(fx, 0xFF8800, 600)
    sys.sleep(2500)
end

led.off()
