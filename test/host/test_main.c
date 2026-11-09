// Test della logica pura (layout tastiera, gesti del pulsante) sul PC.
// Uso: make -C test/host

#include <stdio.h>
#include <string.h>
#include "gesture.h"
#include "hid_layout.h"

static int s_fail;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            s_fail++;                                     \
        }                                                 \
    } while (0)

static void expect_key(const char *layout, hid_os_t os, uint32_t cp, uint8_t key, uint8_t mods)
{
    hid_stroke_t s[8];
    bool np;
    int n = hid_translate(hid_layout_find(layout), os, cp, s, 8, &np);
    CHECK(n == 1 && s[0].key == key && s[0].mods == mods, "%s U+%04X: n=%d key=0x%02X mods=0x%02X", layout,
          (unsigned)cp, n, n ? s[0].key : 0, n ? s[0].mods : 0);
}

static void test_layouts(void)
{
    // US
    expect_key("us", HID_OS_WINDOWS, 'a', 0x04, 0);
    expect_key("us", HID_OS_WINDOWS, 'A', 0x04, HID_MOD_LSHIFT);
    expect_key("us", HID_OS_WINDOWS, '@', 0x1F, HID_MOD_LSHIFT);
    expect_key("us", HID_OS_WINDOWS, '\\', 0x31, 0);
    expect_key("us", HID_OS_WINDOWS, '\n', 0x28, 0);
    // IT
    expect_key("it", HID_OS_WINDOWS, '@', 0x33, HID_MOD_RALT);
    expect_key("it", HID_OS_WINDOWS, '#', 0x34, HID_MOD_RALT);
    expect_key("it", HID_OS_WINDOWS, '"', 0x1F, HID_MOD_LSHIFT);
    expect_key("it", HID_OS_WINDOWS, '/', 0x24, HID_MOD_LSHIFT);
    expect_key("it", HID_OS_WINDOWS, '-', 0x38, 0);
    expect_key("it", HID_OS_WINDOWS, '<', 0x64, 0);
    expect_key("it", HID_OS_WINDOWS, '{', 0x2F, HID_MOD_RALT | HID_MOD_LSHIFT);
    expect_key("it", HID_OS_WINDOWS, 0x20AC, 0x08, HID_MOD_RALT); // €
    expect_key("it", HID_OS_WINDOWS, 0x00E8, 0x2F, 0);           // è
    expect_key("it", HID_OS_LINUX, '~', 0x2E, HID_MOD_RALT);
    // Colemak
    expect_key("colemak", HID_OS_WINDOWS, 'f', 0x08, 0);
    expect_key("colemak", HID_OS_WINDOWS, 'o', 0x33, 0);
    expect_key("colemak", HID_OS_WINDOWS, ';', 0x13, 0);

    // Ripiego Windows: ~ su IT = Alt + 0126 sul tastierino
    hid_stroke_t s[8];
    bool np;
    int n = hid_translate(hid_layout_find("it"), HID_OS_WINDOWS, '~', s, 8, &np);
    CHECK(n == 4 && np && s[0].key == 0x62 && s[1].key == 0x59 && s[2].key == 0x5A && s[3].key == 0x5E,
          "it/windows ~ fallback");
    // Ripiego Linux: ñ (U+00F1) = Ctrl+Shift+U, f, 1, spazio
    n = hid_translate(hid_layout_find("it"), HID_OS_LINUX, 0xF1, s, 8, &np);
    CHECK(n == 4 && s[0].key == 0x18 && s[3].key == 0x2C, "it/linux ñ fallback n=%d", n);
    // macOS non ha ripieghi
    n = hid_translate(hid_layout_find("it"), HID_OS_MAC, '~', s, 8, &np);
    CHECK(n == 0, "mac senza ripiego");

    // Ogni carattere ASCII stampabile è digitabile su US
    for (int c = 0x20; c < 0x7F; c++) {
        n = hid_translate(hid_layout_find("us"), HID_OS_MAC, c, s, 8, &np);
        CHECK(n == 1, "us manca '%c'", c);
    }
    uint8_t key, mods;
    CHECK(hid_key_by_name("F5", &key, &mods) && key == 0x3E, "F5");
    CHECK(hid_key_by_name("ctrl", &key, &mods) && key == 0 && mods == HID_MOD_LCTRL, "ctrl");
}

static gesture_t run(gesture_ctx_t *g, bool pressed, uint32_t *t, uint32_t ms)
{
    gesture_t last = GESTURE_NONE;
    for (uint32_t i = 0; i < ms; i += 10) {
        *t += 10;
        gesture_t e = gesture_feed(g, pressed, *t, NULL);
        if (e != GESTURE_NONE && e != GESTURE_HOLD_PROGRESS && e != GESTURE_LONG_ARMED) last = e;
    }
    return last;
}

static void test_gestures(void)
{
    gesture_ctx_t g;
    uint32_t t = 0;
    gesture_init(&g);
    run(&g, true, &t, 100);
    CHECK(run(&g, false, &t, 400) == GESTURE_CLICK, "click");
    run(&g, true, &t, 100);
    run(&g, false, &t, 100);
    run(&g, true, &t, 100);
    CHECK(run(&g, false, &t, 400) == GESTURE_DOUBLE, "double");
    run(&g, true, &t, 800);
    CHECK(run(&g, false, &t, 400) == GESTURE_LONG, "long");
    run(&g, true, &t, 2000);
    CHECK(run(&g, false, &t, 400) == GESTURE_HOLD_CANCEL, "hold cancel");
    CHECK(run(&g, true, &t, 3100) == GESTURE_HOLD_EXIT, "hold exit");
    CHECK(run(&g, false, &t, 400) == GESTURE_NONE, "release after exit");
}

int main(void)
{
    test_layouts();
    test_gestures();
    printf(s_fail ? "%d test falliti\n" : "tutti i test passati\n", s_fail);
    return s_fail != 0;
}
