#include <glib.h>
#include <stdint.h>

#include "vt100.h"

static int vt100_is_zero_width(uint32_t codepoint);

int vt100_char_width(uint32_t codepoint)
{
    if (vt100_is_zero_width(codepoint)) {
        return 0;
    }
    else if (g_unichar_iswide(codepoint)) {
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
