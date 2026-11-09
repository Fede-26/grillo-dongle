--- @name Mouse
--- @desc Moves the pointer in a square and scrolls. Needs no disk attached.

for _, d in ipairs({{100, 0}, {0, 100}, {-100, 0}, {0, -100}}) do
    mouse.move(d[1], d[2])
    sys.sleep(200)
end
mouse.scroll(-3)
mouse.scroll(3)
