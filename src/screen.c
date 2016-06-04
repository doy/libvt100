#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "vt100.h"
#include "parser.h"

struct vt100_parser_state {
    yyscan_t scanner;
    YY_BUFFER_STATE state;
};

static void vt100_screen_get_string(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp, int formatted);
static void vt100_screen_push_string(char **strp, size_t *lenp,
                                     size_t *capacity, char *append,
                                     size_t append_len);
static void vt100_screen_ensure_capacity(VT100Screen *vt, int size);
static struct vt100_row *vt100_screen_row_at(VT100Screen *vt, int row);
static int vt100_screen_scroll_region_is_active(VT100Screen *vt);
static void vt100_screen_check_wrap(VT100Screen *vt, int width);

VT100Screen *vt100_screen_new(int rows, int cols)
{
    VT100Screen *vt;

    vt = calloc(1, sizeof(VT100Screen));
    vt100_screen_init(vt);
    vt100_screen_set_window_size(vt, rows, cols);

    return vt;
}

void vt100_screen_init(VT100Screen *vt)
{
    vt->grid = calloc(1, sizeof(struct vt100_grid));
    vt->parser_state = calloc(1, sizeof(struct vt100_parser_state));
    vt100_parser_yylex_init_extra(vt, &vt->parser_state->scanner);
}

void vt100_screen_set_window_size(VT100Screen *vt, int rows, int cols)
{
    struct vt100_loc old_size;
    int i;

    old_size.row = vt->grid->max.row;
    old_size.col = vt->grid->max.col;

    vt->grid->max.row = rows;
    vt->grid->max.col = cols;
    if (!vt->custom_scrollback_length)
        vt->scrollback_length = rows;

    if (vt->grid->max.row == 0) {
        vt->grid->max.row = 1;
    }
    if (vt->grid->max.col == 0) {
        vt->grid->max.col = 1;
    }

    if (vt->grid->max.row == old_size.row && vt->grid->max.col == old_size.col) {
        return;
    }

    if (vt->grid->cur.row >= vt->grid->max.row) {
        vt->grid->cur.row = vt->grid->max.row - 1;
    }
    if (vt->grid->cur.col > vt->grid->max.col) {
        vt->grid->cur.col = vt->grid->max.col;
    }

    vt100_screen_ensure_capacity(vt, vt->grid->max.row);

    for (i = 0; i < vt->grid->row_count; ++i) {
        vt->grid->rows[i].cells = realloc(
            vt->grid->rows[i].cells,
            vt->grid->max.col * sizeof(struct vt100_cell));
        if (old_size.col < vt->grid->max.col) {
            memset(
                &vt->grid->rows[i].cells[old_size.col], 0,
                (vt->grid->max.col - old_size.col) * sizeof(struct vt100_cell));
        }
    }

    for (i = vt->grid->row_count; i < vt->grid->max.row; ++i) {
        vt->grid->rows[i].cells = calloc(
            vt->grid->max.col, sizeof(struct vt100_cell));
    }

    if (vt->grid->row_count < vt->grid->max.row) {
        vt->grid->row_count = vt->grid->max.row;
        vt->grid->row_top = 0;
    }
    else {
        vt->grid->row_top = vt->grid->row_count - vt->grid->max.row;
    }

    vt->grid->scroll_top    = 0;
    vt->grid->scroll_bottom = vt->grid->max.row - 1;
}

void vt100_screen_set_scrollback_length(VT100Screen *vt, int rows)
{
    vt->scrollback_length = rows;
    vt->custom_scrollback_length = 1;
}

int vt100_screen_process_string(VT100Screen *vt, char *buf, size_t len)
{
    struct vt100_parser_state *state = vt->parser_state;
    int remaining;

    state->state = vt100_parser_yy_scan_bytes(buf, len, state->scanner);
    remaining = vt100_parser_yylex(state->scanner);
    vt100_parser_yy_delete_buffer(state->state, state->scanner);
    return len - remaining;
}

void vt100_screen_get_string_formatted(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp)
{
    vt100_screen_get_string(vt, start, end, strp, lenp, 1);
}

void vt100_screen_get_string_plaintext(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp)
{
    vt100_screen_get_string(vt, start, end, strp, lenp, 0);
}

struct vt100_cell *vt100_screen_cell_at(VT100Screen *vt, int row, int col)
{
    return &vt->grid->rows[row + vt->grid->row_top].cells[col];
}

void vt100_screen_audible_bell(VT100Screen *vt)
{
    vt->audible_bell = 1;
}

void vt100_screen_visual_bell(VT100Screen *vt)
{
    vt->visual_bell = 1;
}

void vt100_screen_show_string_ascii(VT100Screen *vt, char *buf, size_t len)
{
    size_t i;

    if (len) {
        vt->dirty = 1;

        if (vt->grid->cur.col > 0) {
            struct vt100_cell *cell;

            cell = vt100_screen_cell_at(
                vt, vt->grid->cur.row, vt->grid->cur.col - 1);
            if (cell->is_wide) {
                cell->len = 0;
            }
        }
    }

    for (i = 0; i < len; ++i) {
        struct vt100_cell *cell;

        vt100_screen_check_wrap(vt, 1);
        cell = vt100_screen_cell_at(vt, vt->grid->cur.row, vt->grid->cur.col);

        cell->len = 1;
        cell->contents[0] = buf[i];
        cell->attrs = vt->attrs;
        cell->is_wide = 0;

        vt->grid->cur.col++;
    }
}

void vt100_screen_show_string_utf8(VT100Screen *vt, char *buf, size_t len)
{
    char *c = buf, *next;

    if (len) {
        vt->dirty = 1;

        if (vt->grid->cur.col > 0) {
            struct vt100_cell *cell;

            cell = vt100_screen_cell_at(
                vt, vt->grid->cur.row, vt->grid->cur.col - 1);
            if (cell->is_wide) {
                cell->len = 0;
            }
        }
    }

    while ((next = g_utf8_next_char(c))) {
        gunichar uc;
        struct vt100_cell *cell = NULL;
        int width;

        uc = g_utf8_get_char(c);
        width = vt100_char_width(uc);

        if (width == 0) {
            if (vt->grid->cur.col > 0) {
                cell = vt100_screen_cell_at(
                    vt, vt->grid->cur.row, vt->grid->cur.col - 1);
            }
            else if (vt->grid->cur.row > 0 && vt100_screen_row_at(vt, vt->grid->cur.row - 1)->wrapped) {
                cell = vt100_screen_cell_at(
                    vt, vt->grid->cur.row - 1, vt->grid->max.col - 1);
            }

            if (cell) {
                char *normal;

                memcpy(cell->contents + cell->len, c, next - c);
                cell->len += next - c;
                /* some fonts have combined characters but can't handle
                 * combining characters, so try to fix that here */
                /* XXX it'd be nice if there was a way to do this that didn't
                 * require an allocation */
                normal = g_utf8_normalize(
                    cell->contents, cell->len, G_NORMALIZE_NFC);
                cell->len = strlen(normal);
                memcpy(cell->contents, normal, cell->len);
                free(normal);
            }
        }
        else {
            vt100_screen_check_wrap(vt, width);
            cell = vt100_screen_cell_at(
                vt, vt->grid->cur.row, vt->grid->cur.col);

            cell->len = next - c;
            memcpy(cell->contents, c, cell->len);
            cell->attrs = vt->attrs;
            cell->is_wide = width == 2;

            vt->grid->cur.col += width;
        }

        c = next;
        if ((size_t)(c - buf) >= len) {
            break;
        }
    }
}

void vt100_screen_move_to(VT100Screen *vt, int row, int col)
{
    row = row < 0                  ? 0
        : row >= vt->grid->max.row ? vt->grid->max.row - 1
        : row;
    col = col < 0                  ? 0
        : col >= vt->grid->max.col ? vt->grid->max.col - 1
        : col;

    vt->grid->cur.row = row;
    vt->grid->cur.col = col;
}

void vt100_screen_clear_screen(VT100Screen *vt)
{
    int r;

    for (r = 0; r < vt->grid->max.row; ++r) {
        struct vt100_row *row;

        row = vt100_screen_row_at(vt, r);
        memset(row->cells, 0, vt->grid->max.col * sizeof(struct vt100_cell));
        row->wrapped = 0;
    }

    vt->dirty = 1;
}

void vt100_screen_clear_screen_forward(VT100Screen *vt)
{
    struct vt100_row *row;
    int r;

    row = vt100_screen_row_at(vt, vt->grid->cur.row);
    memset(
        &row->cells[vt->grid->cur.col], 0,
        (vt->grid->max.col - vt->grid->cur.col) * sizeof(struct vt100_cell));
    row->wrapped = 0;
    for (r = vt->grid->cur.row + 1; r < vt->grid->max.row; ++r) {
        row = vt100_screen_row_at(vt, r);
        memset(row->cells, 0, vt->grid->max.col * sizeof(struct vt100_cell));
        row->wrapped = 0;
    }

    vt->dirty = 1;
}

void vt100_screen_clear_screen_backward(VT100Screen *vt)
{
    struct vt100_row *row;
    int r;

    for (r = 0; r < vt->grid->cur.row - 1; ++r) {
        row = vt100_screen_row_at(vt, r);
        memset(row->cells, 0, vt->grid->max.col * sizeof(struct vt100_cell));
        row->wrapped = 0;
    }
    row = vt100_screen_row_at(vt, vt->grid->cur.row);
    memset(row->cells, 0, vt->grid->cur.col * sizeof(struct vt100_cell));

    vt->dirty = 1;
}

void vt100_screen_kill_line(VT100Screen *vt)
{
    struct vt100_row *row;

    row = vt100_screen_row_at(vt, vt->grid->cur.row);
    memset(row->cells, 0, vt->grid->max.col * sizeof(struct vt100_cell));
    row->wrapped = 0;

    vt->dirty = 1;
}

void vt100_screen_kill_line_forward(VT100Screen *vt)
{
    struct vt100_row *row;

    row = vt100_screen_row_at(vt, vt->grid->cur.row);
    memset(
        &row->cells[vt->grid->cur.col], 0,
        (vt->grid->max.col - vt->grid->cur.col) * sizeof(struct vt100_cell));
    row->wrapped = 0;

    vt->dirty = 1;
}

void vt100_screen_kill_line_backward(VT100Screen *vt)
{
    struct vt100_row *row;

    row = vt100_screen_row_at(vt, vt->grid->cur.row);
    memset(row->cells, 0, vt->grid->cur.col * sizeof(struct vt100_cell));
    if (vt->grid->cur.row > 0) {
        row = vt100_screen_row_at(vt, vt->grid->cur.row - 1);
        row->wrapped = 0;
    }

    vt->dirty = 1;
}

void vt100_screen_insert_characters(VT100Screen *vt, int count)
{
    struct vt100_row *row;

    row = vt100_screen_row_at(vt, vt->grid->cur.row);
    if (count >= vt->grid->max.col - vt->grid->cur.col) {
        vt100_screen_kill_line_forward(vt);
    }
    else {
        memmove(
            &row->cells[vt->grid->cur.col + count],
            &row->cells[vt->grid->cur.col],
            (vt->grid->max.col - vt->grid->cur.col - count) * sizeof(struct vt100_cell));
        memset(
            &row->cells[vt->grid->cur.col], 0,
            count * sizeof(struct vt100_cell));
        row->wrapped = 0;
    }

    vt->dirty = 1;
}

void vt100_screen_insert_lines(VT100Screen *vt, int count)
{
    if (count >= vt->grid->max.row - vt->grid->cur.row) {
        vt100_screen_clear_screen_forward(vt);
        vt100_screen_kill_line(vt);
    }
    else {
        struct vt100_row *row;
        int bottom = vt->grid->scroll_bottom + 1;
        int i;

        for (i = bottom - count; i < bottom; ++i) {
            row = vt100_screen_row_at(vt, i);
            free(row->cells);
        }
        row = vt100_screen_row_at(vt, vt->grid->cur.row);
        memmove(
            row + count, row,
            (bottom - vt->grid->cur.row - count) * sizeof(struct vt100_row));
        memset(row, 0, count * sizeof(struct vt100_row));
        for (i = vt->grid->cur.row; i < vt->grid->cur.row + count; ++i) {
            row = vt100_screen_row_at(vt, i);
            row->cells = calloc(vt->grid->max.col, sizeof(struct vt100_cell));
            row->wrapped = 0;
        }
    }

    vt->dirty = 1;
}

void vt100_screen_delete_characters(VT100Screen *vt, int count)
{
    if (count >= vt->grid->max.col - vt->grid->cur.col) {
        vt100_screen_kill_line_forward(vt);
    }
    else {
        struct vt100_row *row;

        row = vt100_screen_row_at(vt, vt->grid->cur.row);
        memmove(
            &row->cells[vt->grid->cur.col],
            &row->cells[vt->grid->cur.col + count],
            (vt->grid->max.col - vt->grid->cur.col - count) * sizeof(struct vt100_cell));
        memset(
            &row->cells[vt->grid->max.col - count], 0,
            count * sizeof(struct vt100_cell));
        row->wrapped = 0;
    }

    vt->dirty = 1;
}

void vt100_screen_delete_lines(VT100Screen *vt, int count)
{
    if (count >= vt->grid->max.row - vt->grid->cur.row) {
        vt100_screen_clear_screen_forward(vt);
        vt100_screen_kill_line(vt);
    }
    else {
        struct vt100_row *row;
        int bottom = vt->grid->scroll_bottom + 1;
        int i;

        for (i = vt->grid->cur.row; i < vt->grid->cur.row + count; ++i) {
            row = vt100_screen_row_at(vt, i);
            free(row->cells);
        }
        row = vt100_screen_row_at(vt, vt->grid->cur.row);
        memmove(
            row, row + count,
            (bottom - vt->grid->cur.row - count) * sizeof(struct vt100_row));
        row = vt100_screen_row_at(vt, bottom - count);
        memset(row, 0, count * sizeof(struct vt100_row));
        for (i = bottom - count; i < bottom; ++i) {
            row = vt100_screen_row_at(vt, i);
            row->cells = calloc(vt->grid->max.col, sizeof(struct vt100_cell));
            row->wrapped = 0;
        }
    }

    vt->dirty = 1;
}

void vt100_screen_erase_characters(VT100Screen *vt, int count)
{
    if (count >= vt->grid->max.col - vt->grid->cur.col) {
        vt100_screen_kill_line_forward(vt);
    }
    else {
        struct vt100_row *row;
        int i;

        row = vt100_screen_row_at(vt, vt->grid->cur.row);

        for (i = vt->grid->cur.col; i < vt->grid->cur.col + count; ++i) {
            struct vt100_cell *cell = &row->cells[i];

            cell->len = 0;
        }
    }

    vt->dirty = 1;
}

void vt100_screen_scroll_down(VT100Screen *vt, int count)
{
    struct vt100_row *row;
    int bottom = vt->grid->scroll_bottom, top = vt->grid->scroll_top;
    int i;

    if (bottom - top + 1 > count) {
        for (i = 0; i < count; ++i) {
            row = vt100_screen_row_at(vt, bottom - i);
            free(row->cells);
        }
        row = vt100_screen_row_at(vt, top);
        memmove(
            row + count, row,
            (bottom - top + 1 - count) * sizeof(struct vt100_row));
        for (i = 0; i < count; ++i) {
            row = vt100_screen_row_at(vt, top + i);
            row->cells = calloc(vt->grid->max.col, sizeof(struct vt100_cell));
            row->wrapped = 0;
        }
    }
    else {
        for (i = 0; i < bottom - top + 1; ++i) {
            row = vt100_screen_row_at(vt, top + i);
            memset(
                row->cells, 0, vt->grid->max.col * sizeof(struct vt100_cell));
            row->wrapped = 0;
        }
    }

    vt->dirty = 1;
}

void vt100_screen_scroll_up(VT100Screen *vt, int count)
{
    struct vt100_row *row;
    int i;

    if (vt100_screen_scroll_region_is_active(vt) || vt->alternate) {
        int bottom = vt->grid->scroll_bottom, top = vt->grid->scroll_top;

        if (bottom - top + 1 > count) {
            for (i = 0; i < count; ++i) {
                row = vt100_screen_row_at(vt, top + i);
                free(row->cells);
            }
            row = vt100_screen_row_at(vt, top);
            memmove(
                row, row + count,
                (bottom - top + 1 - count) * sizeof(struct vt100_row));
            for (i = 0; i < count; ++i) {
                row = vt100_screen_row_at(vt, bottom - i);
                row->cells = calloc(
                    vt->grid->max.col, sizeof(struct vt100_cell));
                row->wrapped = 0;
            }
        }
        else {
            for (i = 0; i < bottom - top + 1; ++i) {
                row = vt100_screen_row_at(vt, top + i);
                memset(
                    row->cells, 0,
                    vt->grid->max.col * sizeof(struct vt100_cell));
                row->wrapped = 0;
            }
        }
    }
    else {
        int scrollback = vt->scrollback_length;

        if (vt->grid->row_count + count > scrollback) {
            int overflow = vt->grid->row_count + count - scrollback;

            vt100_screen_ensure_capacity(vt, scrollback);
            for (i = 0; i < overflow; ++i) {
                free(vt->grid->rows[i].cells);
            }
            memmove(
                &vt->grid->rows[0], &vt->grid->rows[overflow],
                (scrollback - overflow) * sizeof(struct vt100_row));
            for (i = scrollback - count; i < scrollback; ++i) {
                vt->grid->rows[i].cells = calloc(
                    vt->grid->max.col, sizeof(struct vt100_cell));
            }
            vt->grid->row_count = scrollback;
            vt->grid->row_top = scrollback - vt->grid->max.row;
        }
        else {
            vt100_screen_ensure_capacity(vt, vt->grid->row_count + count);
            for (i = 0; i < count; ++i) {
                row = vt100_screen_row_at(vt, i + vt->grid->max.row);
                row->cells = calloc(
                    vt->grid->max.col, sizeof(struct vt100_cell));
            }
            vt->grid->row_count += count;
            vt->grid->row_top += count;
        }
    }

    vt->dirty = 1;
}

void vt100_screen_move_down_or_scroll(VT100Screen *vt)
{
    if (vt->grid->cur.row == vt->grid->scroll_bottom) {
        vt100_screen_scroll_up(vt, 1);
    }
    else {
        vt100_screen_move_to(vt, vt->grid->cur.row + 1, vt->grid->cur.col);
    }
}

void vt100_screen_move_up_or_scroll(VT100Screen *vt)
{
    if (vt->grid->cur.row == vt->grid->scroll_top) {
        vt100_screen_scroll_down(vt, 1);
    }
    else {
        vt100_screen_move_to(vt, vt->grid->cur.row - 1, vt->grid->cur.col);
    }
}

void vt100_screen_set_scroll_region(
    VT100Screen *vt, int top, int bottom, int left, int right)
{
    if (left > 0 || right < vt->grid->max.col - 1) {
        fprintf(stderr, "vertical scroll regions not yet implemented\n");
    }

    if (top > bottom) {
        return;
    }

    vt->grid->scroll_top = top < 0
        ? 0
        : top;
    vt->grid->scroll_bottom = bottom >= vt->grid->max.row
        ? vt->grid->max.row - 1
        : bottom;
}

void vt100_screen_reset_text_attributes(VT100Screen *vt)
{
    memset(&vt->attrs, 0, sizeof(struct vt100_cell_attrs));
}

void vt100_screen_set_fg_color(VT100Screen *vt, int idx)
{
    vt->attrs.fgcolor.type = VT100_COLOR_IDX;
    vt->attrs.fgcolor.idx = idx;
}

void vt100_screen_set_fg_color_rgb(
    VT100Screen *vt, unsigned char r, unsigned char g, unsigned char b)
{
    vt->attrs.fgcolor.type = VT100_COLOR_RGB;
    vt->attrs.fgcolor.r = r;
    vt->attrs.fgcolor.g = g;
    vt->attrs.fgcolor.b = b;
}

void vt100_screen_reset_fg_color(VT100Screen *vt)
{
    vt->attrs.fgcolor.type = VT100_COLOR_DEFAULT;
}

void vt100_screen_set_bg_color(VT100Screen *vt, int idx)
{
    vt->attrs.bgcolor.type = VT100_COLOR_IDX;
    vt->attrs.bgcolor.idx = idx;
}

void vt100_screen_set_bg_color_rgb(
    VT100Screen *vt, unsigned char r, unsigned char g, unsigned char b)
{
    vt->attrs.bgcolor.type = VT100_COLOR_RGB;
    vt->attrs.bgcolor.r = r;
    vt->attrs.bgcolor.g = g;
    vt->attrs.bgcolor.b = b;
}

void vt100_screen_reset_bg_color(VT100Screen *vt)
{
    vt->attrs.bgcolor.type = VT100_COLOR_DEFAULT;
}

void vt100_screen_set_bold(VT100Screen *vt)
{
    vt->attrs.bold = 1;
}

void vt100_screen_set_italic(VT100Screen *vt)
{
    vt->attrs.italic = 1;
}

void vt100_screen_set_underline(VT100Screen *vt)
{
    vt->attrs.underline = 1;
}

void vt100_screen_set_inverse(VT100Screen *vt)
{
    vt->attrs.inverse = 1;
}

void vt100_screen_reset_bold(VT100Screen *vt)
{
    vt->attrs.bold = 0;
}

void vt100_screen_reset_italic(VT100Screen *vt)
{
    vt->attrs.italic = 0;
}

void vt100_screen_reset_underline(VT100Screen *vt)
{
    vt->attrs.underline = 0;
}

void vt100_screen_reset_inverse(VT100Screen *vt)
{
    vt->attrs.inverse = 0;
}

void vt100_screen_use_alternate_buffer(VT100Screen *vt)
{
    if (vt->alternate) {
        return;
    }

    vt->alternate = vt->grid;
    vt->grid = calloc(1, sizeof(struct vt100_grid));
    vt100_screen_set_window_size(
        vt, vt->alternate->max.row, vt->alternate->max.col
    );

    vt->dirty = 1;
}

void vt100_screen_use_normal_buffer(VT100Screen *vt)
{
    int i;

    if (!vt->alternate) {
        return;
    }

    for (i = 0; i < vt->grid->row_count; ++i) {
        free(vt->grid->rows[i].cells);
    }
    free(vt->grid->rows);
    free(vt->grid);

    vt->grid = vt->alternate;
    vt->alternate = NULL;

    vt100_screen_set_window_size(vt, vt->grid->max.row, vt->grid->max.col);

    vt->dirty = 1;
}

void vt100_screen_save_cursor(VT100Screen *vt)
{
    vt->grid->saved = vt->grid->cur;
}

void vt100_screen_restore_cursor(VT100Screen *vt)
{
    vt->grid->cur = vt->grid->saved;
}

void vt100_screen_show_cursor(VT100Screen *vt)
{
    vt->hide_cursor = 0;
}

void vt100_screen_hide_cursor(VT100Screen *vt)
{
    vt->hide_cursor = 1;
}

void vt100_screen_set_application_keypad(VT100Screen *vt)
{
    vt->application_keypad = 1;
}

void vt100_screen_reset_application_keypad(VT100Screen *vt)
{
    vt->application_keypad = 0;
}

void vt100_screen_set_application_cursor(VT100Screen *vt)
{
    vt->application_cursor = 1;
}

void vt100_screen_reset_application_cursor(VT100Screen *vt)
{
    vt->application_cursor = 0;
}

void vt100_screen_set_mouse_reporting_press(VT100Screen *vt)
{
    vt->mouse_reporting_press = 1;
}

void vt100_screen_reset_mouse_reporting_press(VT100Screen *vt)
{
    vt->mouse_reporting_press = 0;
}

void vt100_screen_set_mouse_reporting_press_release(VT100Screen *vt)
{
    vt->mouse_reporting_press_release = 1;
}

void vt100_screen_reset_mouse_reporting_press_release(VT100Screen *vt)
{
    vt->mouse_reporting_press_release = 0;
}

void vt100_screen_set_mouse_reporting_button_motion(VT100Screen *vt)
{
    vt->mouse_reporting_button_motion = 1;
}

void vt100_screen_reset_mouse_reporting_button_motion(VT100Screen *vt)
{
    vt->mouse_reporting_button_motion = 0;
}

void vt100_screen_set_mouse_reporting_sgr_mode(VT100Screen *vt)
{
    vt->mouse_reporting_sgr_mode = 1;
}

void vt100_screen_reset_mouse_reporting_sgr_mode(VT100Screen *vt)
{
    vt->mouse_reporting_sgr_mode = 0;
}

void vt100_screen_set_bracketed_paste(VT100Screen *vt)
{
    vt->bracketed_paste = 1;
}

void vt100_screen_reset_bracketed_paste(VT100Screen *vt)
{
    vt->bracketed_paste = 0;
}

void vt100_screen_set_window_title(VT100Screen *vt, char *buf, size_t len)
{
    free(vt->title);
    vt->title_len = len;
    vt->title = malloc(vt->title_len);
    memcpy(vt->title, buf, vt->title_len);
    vt->update_title = 1;
}

void vt100_screen_set_icon_name(VT100Screen *vt, char *buf, size_t len)
{
    free(vt->icon_name);
    vt->icon_name_len = len;
    vt->icon_name = malloc(vt->icon_name_len);
    memcpy(vt->icon_name, buf, vt->icon_name_len);
    vt->update_icon_name = 1;
}

int vt100_screen_row_max_col(VT100Screen *vt, int row)
{
    struct vt100_cell *cells = vt->grid->rows[row].cells;
    int i, max = -1;

    for (i = 0; i < vt->grid->max.col; ++i) {
        if (cells[i].len) {
            max = i;
        }
    }

    return max + 1;
}

void vt100_screen_cleanup(VT100Screen *vt)
{
    int i;

    for (i = 0; i < vt->grid->row_count; ++i) {
        free(vt->grid->rows[i].cells);
    }
    free(vt->grid->rows);
    free(vt->grid);

    free(vt->title);
    free(vt->icon_name);

    vt100_parser_yylex_destroy(vt->parser_state->scanner);
    free(vt->parser_state);
}

void vt100_screen_delete(VT100Screen *vt)
{
    vt100_screen_cleanup(vt);
    free(vt);
}

static void vt100_screen_get_string(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp, int formatted)
{
    int row, col;
    size_t capacity = 8;
    struct vt100_cell_attrs attrs;

    memset(&attrs, 0, sizeof(struct vt100_cell_attrs));

    *lenp = 0;

    if (end->row < start->row || (end->row == start->row && end->col <= start->col)) {
        *strp = NULL;
        return;
    }

    *strp = malloc(capacity);

    for (row = start->row; row <= end->row; ++row) {
        int start_col, end_col, max_col, was_wide = 0;
        struct vt100_row *grid_row = &vt->grid->rows[row];

        max_col = vt100_screen_row_max_col(vt, row);

        if (row == start->row) {
            if (start->col > max_col) {
                start_col = vt->grid->max.col;
            }
            else {
                start_col = start->col;
            }
        }
        else {
            start_col = 0;
        }

        if (row == end->row) {
            if (end->col > max_col) {
                end_col = vt->grid->max.col;
            }
            else {
                end_col = end->col;
            }
        }
        else {
            end_col = vt->grid->max.col;
        }

        if (end_col > max_col) {
            end_col = max_col;
        }

        for (col = start_col; col < end_col; ++col) {
            struct vt100_cell *cell = &grid_row->cells[col];
            char *contents = cell->contents;
            size_t len = cell->len;

            if (formatted
                && memcmp(&attrs, &cell->attrs,
                          sizeof(struct vt100_cell_attrs))) {
                int attr_codes[6] = { 0 };
                int first = 1;
                size_t i;

                if (attrs.fgcolor.id != cell->attrs.fgcolor.id) {
                    switch (cell->attrs.fgcolor.type) {
                    case VT100_COLOR_DEFAULT:
                        attr_codes[0] = 39;
                        break;
                    case VT100_COLOR_IDX:
                        attr_codes[0] = 30 + cell->attrs.fgcolor.idx;
                        break;
                    case VT100_COLOR_RGB:
                        // XXX
                        break;
                    }
                }
                if (attrs.bgcolor.id != cell->attrs.bgcolor.id) {
                    switch (cell->attrs.bgcolor.type) {
                    case VT100_COLOR_DEFAULT:
                        attr_codes[1] = 49;
                        break;
                    case VT100_COLOR_IDX:
                        attr_codes[1] = 40 + cell->attrs.bgcolor.idx;
                        break;
                    case VT100_COLOR_RGB:
                        // XXX
                        break;
                    }
                }
                if (attrs.bold != cell->attrs.bold) {
                    attr_codes[2] = cell->attrs.bold ? 1 : 21;
                }
                if (attrs.italic != cell->attrs.italic) {
                    attr_codes[3] = cell->attrs.italic ? 3 : 23;
                }
                if (attrs.underline != cell->attrs.underline) {
                    attr_codes[4] = cell->attrs.underline ? 4 : 24;
                }
                if (attrs.inverse != cell->attrs.inverse) {
                    attr_codes[5] = cell->attrs.inverse ? 7 : 27;
                }
                vt100_screen_push_string(strp, lenp, &capacity, "\033[", 2);
                for (i = 0; i < sizeof(attr_codes) / sizeof(int); ++i) {
                    char buf[3];

                    if (!attr_codes[i]) {
                        continue;
                    }

                    if (!first) {
                        vt100_screen_push_string(strp, lenp, &capacity, ";", 1);
                    }
                    sprintf(buf, "%d", attr_codes[i]);
                    vt100_screen_push_string(strp, lenp, &capacity, buf,
                                             strlen(buf));

                    first = 0;
                }
                vt100_screen_push_string(strp, lenp, &capacity, "m", 1);
                memcpy(&attrs, &cell->attrs, sizeof(struct vt100_cell_attrs));
            }

            if (!was_wide) {
                if (cell->len == 0) {
                    contents = " ";
                    len = 1;
                }

                vt100_screen_push_string(strp, lenp, &capacity, contents, len);
            }

            was_wide = cell->is_wide;
        }

        if ((row != end->row || end->col > max_col) && !grid_row->wrapped) {
            vt100_screen_push_string(strp, lenp, &capacity, "\n", 1);
        }
    }
}

static void vt100_screen_push_string(char **strp, size_t *lenp,
                                     size_t *capacity, char *append,
                                     size_t append_len)
{
    if (*lenp + append_len > *capacity) {
        *capacity *= 1.5;
        *strp = realloc(*strp, *capacity);
    }
    memcpy(*strp + *lenp, append, append_len);
    *lenp += append_len;
}

static void vt100_screen_ensure_capacity(VT100Screen *vt, int size)
{
    int old_capacity = vt->grid->row_capacity;

    if (vt->grid->row_capacity >= size) {
        return;
    }

    if (vt->grid->row_capacity == 0) {
        vt->grid->row_capacity = vt->grid->max.row;
    }

    while (vt->grid->row_capacity < size) {
        vt->grid->row_capacity *= 1.5;
    }

    vt->grid->rows = realloc(
        vt->grid->rows, vt->grid->row_capacity * sizeof(struct vt100_row));
    memset(
        &vt->grid->rows[old_capacity], 0,
        (vt->grid->row_capacity - old_capacity) * sizeof(struct vt100_row));
}

static struct vt100_row *vt100_screen_row_at(VT100Screen *vt, int row)
{
    return &vt->grid->rows[row + vt->grid->row_top];
}

static int vt100_screen_scroll_region_is_active(VT100Screen *vt)
{
    return vt->grid->scroll_top != 0
        || vt->grid->scroll_bottom != vt->grid->max.row - 1;
}

static void vt100_screen_check_wrap(VT100Screen *vt, int width)
{
    if (vt->grid->cur.col + width > vt->grid->max.col) {
        vt100_screen_row_at(vt, vt->grid->cur.row)->wrapped = 1;
        vt100_screen_move_down_or_scroll(vt);
        vt->grid->cur.col = 0;
    }
}
