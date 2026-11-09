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
    // Colemak su Linux (xkb us(colemak)): AltGr+a = á, AltGr+e = é, € = AltGr+5
    expect_key("colemak", HID_OS_LINUX, 0x00E1, 0x04, HID_MOD_RALT);
    expect_key("colemak", HID_OS_LINUX, 0x00E9, 0x0E, HID_MOD_RALT); // la 'e' Colemak sta sul tasto fisico 'k' US
    expect_key("colemak", HID_OS_LINUX, 0x20AC, 0x22, HID_MOD_RALT);

    // Ripiego Windows: ~ su IT = Alt + 0126 sul tastierino
    hid_stroke_t s[8];
    bool np;
    int n = hid_translate(hid_layout_find("it"), HID_OS_WINDOWS, '~', s, 8, &np);
    CHECK(n == 4 && np && s[0].key == 0x62 && s[1].key == 0x59 && s[2].key == 0x5A && s[3].key == 0x5E,
          "it/windows ~ fallback");
    // Ripiego Linux: ñ (U+00F1) = Ctrl+Shift+U, f, 1, spazio
    n = hid_translate(hid_layout_find("it"), HID_OS_LINUX, 0xF1, s, 8, &np);
    CHECK(n == 4 && s[0].key == 0x18 && s[3].key == 0x2C, "it/linux ñ fallback n=%d", n);
    // Colemak su Linux: à = AltGr+r (tasto morto grave, posizione fisica 's' = 0x16), poi a (0x04)
    n = hid_translate(hid_layout_find("colemak"), HID_OS_LINUX, 0x00E0, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x16 && s[0].mods == HID_MOD_RALT && s[1].key == 0x04 && s[1].mods == 0,
          "colemak/linux à n=%d", n);
    // È: stessa sequenza, con Shift sulla 'e' (tasto fisico 0x0E)
    n = hid_translate(hid_layout_find("colemak"), HID_OS_LINUX, 0x00C8, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x16 && s[1].key == 0x0E && (s[1].mods & HID_MOD_LSHIFT), "colemak/linux È n=%d", n);
    // Colemak su Windows (KLC ufficiale): AltGr+a = á, à = AltGr+r poi a. Niente ripiego numpad
    expect_key("colemak", HID_OS_WINDOWS, 0x00E1, 0x04, HID_MOD_RALT);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_WINDOWS, 0x00E0, s, 8, &np);
    CHECK(n == 2 && !np && s[0].key == 0x16 && s[0].mods == HID_MOD_RALT && s[1].key == 0x04,
          "colemak/windows à n=%d", n);
    // Un carattere che il Colemak Windows non ha (Ω) resta sul ripiego Alt+tastierino se rappresentabile: qui no
    n = hid_translate(hid_layout_find("colemak"), HID_OS_WINDOWS, 0x03A9, s, 8, &np);
    CHECK(n == 0, "colemak/windows Ω non digitabile n=%d", n);
    // Colemak su macOS (da UCKeyTranslate): Option+a = å, Option+` poi a = à, Option+e poi e = é (la e Colemak sta sul tasto fisico 0x0E, anche come tasto morto)
    expect_key("colemak", HID_OS_MAC, 0x00E5, 0x04, HID_MOD_LALT);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_MAC, 0x00E0, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x35 && s[0].mods == HID_MOD_LALT && s[1].key == 0x04,
          "colemak/mac à n=%d key0=0x%02X mods0=0x%02X", n, n ? s[0].key : 0, n ? s[0].mods : 0);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_MAC, 0x00E9, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x0E && s[0].mods == HID_MOD_LALT && s[1].key == 0x0E, "colemak/mac é n=%d", n);
    // macOS "Italian - Pro" (da UCKeyTranslate): ~ = Option+5, ` = Option+\ (tasto ISO a sinistra dell'1),
    // @ e € come su Windows (Option vale AltGr), à diretta, À = Option+Shift+w
    expect_key("it", HID_OS_MAC, '~', 0x22, HID_MOD_LALT);
    expect_key("it", HID_OS_MAC, '`', 0x35, HID_MOD_LALT);
    expect_key("it", HID_OS_MAC, '\\', 0x35, 0);
    expect_key("it", HID_OS_MAC, '<', 0x64, 0);
    expect_key("it", HID_OS_MAC, '@', 0x33, HID_MOD_RALT);
    expect_key("it", HID_OS_MAC, 0x00E0, 0x34, 0);
    expect_key("it", HID_OS_MAC, 0x00C0, 0x1A, HID_MOD_LALT | HID_MOD_LSHIFT); // À = Option+Shift+w
    // macOS non ha ripieghi per ciò che il layout non produce
    n = hid_translate(hid_layout_find("it"), HID_OS_MAC, 0x4E2D, s, 8, &np);
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
