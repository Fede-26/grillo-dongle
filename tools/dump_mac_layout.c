// Dumps from macOS the real map of a keyboard layout (normal, Shift, Option,
// Option+Shift levels and dead keys) with UCKeyTranslate.
//
//   cc -o dump_mac_layout dump_mac_layout.c -framework Carbon -framework CoreFoundation
//   ./dump_mac_layout com.apple.keylayout.Colemak > layouts/macos_colemak.tsv
//
// One line per physical key: "hid<TAB>l0<TAB>l1<TAB>l2<TAB>l3". A level is the
// character produced, "DEAD:<combining character>" for a dead key, "-" if
// nothing. Keys have the HID usages of the US position (like gen_layouts.py).
// After the table, a line "C<TAB>hid<TAB>level<TAB>letter<TAB>result" for
// each composition defined by the layout (dead key then letter a-zA-Z).
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <string.h>

// macOS virtual keycode (ANSI) -> HID usage
static const struct { int mac, hid; } KEYS[] = {
    {50, 0x35}, {18, 0x1E}, {19, 0x1F}, {20, 0x20}, {21, 0x21}, {23, 0x22}, {22, 0x23}, {26, 0x24}, {28, 0x25},
    {25, 0x26}, {29, 0x27}, {27, 0x2D}, {24, 0x2E},
    {12, 0x14}, {13, 0x1A}, {14, 0x08}, {15, 0x15}, {17, 0x17}, {16, 0x1C}, {32, 0x18}, {34, 0x0C}, {31, 0x12},
    {35, 0x13}, {33, 0x2F}, {30, 0x30},
    {0, 0x04}, {1, 0x16}, {2, 0x07}, {3, 0x09}, {5, 0x0A}, {4, 0x0B}, {38, 0x0D}, {40, 0x0E}, {37, 0x0F},
    {41, 0x33}, {39, 0x34}, {42, 0x31},
    {10, 0x64}, {6, 0x1D}, {7, 0x1B}, {8, 0x06}, {9, 0x19}, {11, 0x05}, {45, 0x11}, {46, 0x10}, {43, 0x36},
    {47, 0x37}, {44, 0x38},
};
static const int MODS[4] = {0, 2, 8, 10}; // (shift = 2, option = 8) >> 8 of the Carbon mask

static int translate(const UCKeyboardLayout *l, int key, int mod, UInt32 *dead, UniChar *out, int max)
{
    UniCharCount n = 0;
    UCKeyTranslate(l, key, kUCKeyActionDown, mod, LMGetKbdType(), 0, dead, max, &n, out);
    return (int)n;
}

static void put_utf16(const UniChar *u, int n)
{
    CFStringRef s = CFStringCreateWithCharacters(NULL, u, n);
    char buf[16];
    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
    fputs(buf, stdout);
    CFRelease(s);
}

int main(int argc, char **argv)
{
    CFStringRef id = CFStringCreateWithCString(NULL, argc > 1 ? argv[1] : "com.apple.keylayout.Colemak",
                                               kCFStringEncodingUTF8);
    const void *k[] = {kTISPropertyInputSourceID};
    const void *v[] = {id};
    CFDictionaryRef f = CFDictionaryCreate(NULL, k, v, 1, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFArrayRef list = TISCreateInputSourceList(f, true);
    if (!list || !CFArrayGetCount(list)) {
        fprintf(stderr, "layout not found\n");
        return 1;
    }
    TISInputSourceRef src = (TISInputSourceRef)CFArrayGetValueAtIndex(list, 0);
    CFDataRef data = TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData);
    const UCKeyboardLayout *l = (const UCKeyboardLayout *)CFDataGetBytePtr(data);

    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        printf("%02x", KEYS[i].hid);
        for (int m = 0; m < 4; m++) {
            UInt32 dead = 0;
            UniChar out[8];
            int n = translate(l, KEYS[i].mac, MODS[m], &dead, out, 8);
            putchar('\t');
            if (n == 0 && dead) {
                // Dead key: try composing it with some letters to discover the
                // combining mark (NFD of the first single-character result)
                const char *letters = "aeiouAEIOUcCnN";
                int found = 0;
                for (const char *c = letters; *c && !found; c++) {
                    int lk = -1;
                    for (size_t j = 0; j < sizeof(KEYS) / sizeof(KEYS[0]); j++) {
                        UInt32 d2 = 0;
                        UniChar o2[4];
                        int n2 = translate(l, KEYS[j].mac, 0, &d2, o2, 4);
                        if (n2 == 1 && o2[0] == (UniChar)*c) lk = KEYS[j].mac;
                        (void)n2;
                    }
                    // uppercase letters: they need Shift
                    int lmod = (*c >= 'A' && *c <= 'Z') ? 2 : 0;
                    if (lk < 0 && lmod) {
                        for (size_t j = 0; j < sizeof(KEYS) / sizeof(KEYS[0]); j++) {
                            UInt32 d2 = 0;
                            UniChar o2[4];
                            if (translate(l, KEYS[j].mac, 2, &d2, o2, 4) == 1 && o2[0] == (UniChar)*c) lk = KEYS[j].mac;
                        }
                    }
                    if (lk < 0) continue;
                    UInt32 d3 = dead;
                    UniChar o3[8];
                    int n3 = translate(l, lk, lmod, &d3, o3, 8);
                    if (n3 == 1 && o3[0] != (UniChar)*c) {
                        CFMutableStringRef s = CFStringCreateMutable(NULL, 0);
                        CFStringAppendCharacters(s, o3, 1);
                        CFStringNormalize(s, kCFStringNormalizationFormD);
                        CFIndex len = CFStringGetLength(s);
                        if (len == 2) {
                            UniChar mark = CFStringGetCharacterAtIndex(s, 1);
                            printf("DEAD:");
                            put_utf16(&mark, 1);
                            found = 1;
                        }
                        CFRelease(s);
                    }
                }
                if (!found) printf("DEAD:?");
            } else if (n == 0) {
                printf("-");
            } else {
                put_utf16(out, n);
            }
        }
        putchar('\n');
    }

    // Compositions: for every dead key and every letter, what the layout produces
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        for (int m = 0; m < 4; m++) {
            UInt32 dead = 0;
            UniChar out[8];
            if (translate(l, KEYS[i].mac, MODS[m], &dead, out, 8) != 0 || !dead) continue;
            for (const char *c = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"; *c; c++) {
                int lk = -1, lmod = 0;
                for (size_t j = 0; j < sizeof(KEYS) / sizeof(KEYS[0]) && lk < 0; j++) {
                    for (int sm = 0; sm < 2 && lk < 0; sm++) {
                        UInt32 d2 = 0;
                        UniChar o2[4];
                        if (translate(l, KEYS[j].mac, MODS[sm], &d2, o2, 4) == 1 && o2[0] == (UniChar)*c) {
                            lk = KEYS[j].mac;
                            lmod = MODS[sm];
                        }
                    }
                }
                if (lk < 0) continue;
                UInt32 d3 = dead;
                UniChar o3[8];
                int n3 = translate(l, lk, lmod, &d3, o3, 8);
                if (n3 == 1 && o3[0] != (UniChar)*c) {
                    printf("C\t%02x\t%d\t%c\t", KEYS[i].hid, m, *c);
                    put_utf16(o3, 1);
                    putchar('\n');
                }
            }
        }
    }
    return 0;
}
