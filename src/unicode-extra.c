#include <glib.h>
#include <stdint.h>

#include "vt100.h"

/*
 * so, here's the story. unicode doesn't actually define monospace width for
 * characters in a way that's useful. there's an "east asian width" property
 * that mostly works, but leaves a bunch of things ambiguous (if you're
 * displaying this as part of a bunch of east asian text, then it's wide, but
 * if you're not, then it's narrow). we in general treat ambiguous characters
 * as narrow for now (although this should perhaps be an option in the future).
 * one place where this does not work out, though, is emoji. emoji do not have
 * a useful width property (see
 * http://www.unicode.org/L2/L2016/16027-emoji-terminals-eaw.pdf), and even the
 * proposal in that link to make every character with Emoji_Presentation=true a
 * wide character isn't really how things work in practice - for instance,
 * U+231A (WATCH) is rendered by most monospace fonts (that I've seen, anyway)
 * as a narrow character. as far as i can tell, it appears (although i'm not
 * certain of this) that all BMP characters with Emoji_Presentation=true are
 * narrow and all astral plane characters with Emoji_Presentation=true are
 * wide, so that's what i'm going to go with here. character ranges and data in
 * this file are taken from
 * http://www.unicode.org/Public/emoji/2.0//emoji-data.txt.
 */
struct vt100_char_range {
    uint32_t start;
    uint32_t end;
};

static struct vt100_char_range vt100_wide_emoji[] = {
    { 0x1F004, 0x1F004 },
    { 0x1F0CF, 0x1F0CF },
    { 0x1F18E, 0x1F18E },
    { 0x1F191, 0x1F19A },
    { 0x1F1E6, 0x1F1FF },
    { 0x1F201, 0x1F201 },
    { 0x1F21A, 0x1F21A },
    { 0x1F22F, 0x1F22F },
    { 0x1F232, 0x1F236 },
    { 0x1F238, 0x1F23A },
    { 0x1F250, 0x1F251 },
    { 0x1F300, 0x1F320 },
    { 0x1F32D, 0x1F335 },
    { 0x1F337, 0x1F37C },
    { 0x1F37E, 0x1F393 },
    { 0x1F3A0, 0x1F3CA },
    { 0x1F3CF, 0x1F3D3 },
    { 0x1F3E0, 0x1F3F0 },
    { 0x1F3F4, 0x1F3F4 },
    { 0x1F3F8, 0x1F43E },
    { 0x1F440, 0x1F440 },
    { 0x1F442, 0x1F4FC },
    { 0x1F4FF, 0x1F53D },
    { 0x1F54B, 0x1F54E },
    { 0x1F550, 0x1F567 },
    { 0x1F595, 0x1F596 },
    { 0x1F5FB, 0x1F64F },
    { 0x1F680, 0x1F6C5 },
    { 0x1F6CC, 0x1F6CC },
    { 0x1F6D0, 0x1F6D0 },
    { 0x1F6EB, 0x1F6EC },
    { 0x1F910, 0x1F918 },
    { 0x1F980, 0x1F984 },
    { 0x1F9C0, 0x1F9C0 },
};

static int vt100_is_zero_width(uint32_t codepoint);
static int vt100_is_wide_char(uint32_t codepoint);
static int vt100_is_wide_emoji(uint32_t codepoint);

int vt100_char_width(uint32_t codepoint)
{
    if (vt100_is_zero_width(codepoint)) {
        return 0;
    }
    else if (vt100_is_wide_char(codepoint)) {
        return 2;
    }
    else {
        return 1;
    }
}

static int vt100_is_zero_width(uint32_t codepoint)
{
    /* we want soft hyphens to actually be zero width, because terminals don't
     * do word wrapping */
    return g_unichar_iszerowidth(codepoint) || codepoint == 0xAD;
}

static int vt100_is_wide_char(uint32_t codepoint)
{
    return g_unichar_iswide(codepoint) || vt100_is_wide_emoji(codepoint);
}

static int vt100_is_wide_emoji(uint32_t codepoint)
{
    static size_t ranges = sizeof(vt100_wide_emoji) / sizeof(struct vt100_char_range);
    size_t low = 0, high = ranges - 1;

    if (codepoint < vt100_wide_emoji[0].start) {
        return 0;
    }

    do {
        size_t cur = (high + low) / 2;
        struct vt100_char_range range = vt100_wide_emoji[cur];

        if (codepoint < range.start) {
            high = cur - 1;
        }
        else if (codepoint > range.end) {
            low = cur + 1;
        }
        else {
            return 1;
        }
    } while (low <= high);

    return 0;
}
