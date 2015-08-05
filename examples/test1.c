#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vt100.h"

int main(int argc, char *argv[])
{
    VT100Screen *vt;
    char buf[4096];
    size_t offset = 0;
    int i, j, skip;

    vt = vt100_screen_new(24, 80);
    // vt100_screen_set_window_size(vt);

    for (;;) {
        size_t bytes, parsed;

        bytes = fread(buf + offset, 1, 4096 - offset, stdin);
        if (bytes < 1)
            break;

        parsed = vt100_screen_process_string(vt, buf, bytes + offset);
        if (parsed < bytes + offset) {
            memcpy(buf, buf + parsed, bytes - parsed);
            offset = bytes - parsed;
        }
    }

    skip = 0;
    for (i = vt->grid->row_top; i < vt->grid->row_top + vt->grid->max.row; ++i) {
        for (j = 0; j < vt->grid->max.col; ++j) {
            if (skip) {
                skip = 0;
                continue;
            }
            struct vt100_cell *cell = &vt->grid->rows[i].cells[j];
            printf("%*s", cell->len, cell->contents);
            if (cell->is_wide)
                skip = 1;
        }
        printf("\n");
    }

    vt100_screen_delete(vt);

    return 0;
}
