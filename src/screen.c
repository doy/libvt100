#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "vt100.h"
#include "parser.h"

static void vt100_screen_ensure_capacity(VT100Screen *vt, int size);
static struct vt100_row *vt100_screen_row_at(VT100Screen *vt, int row);
static struct vt100_cell *vt100_screen_cell_at(VT100Screen *vt, int row, int col);
static void vt100_screen_scroll_down(VT100Screen *vt, int count);
static void vt100_screen_scroll_up(VT100Screen *vt, int count);
static int vt100_screen_scroll_region_is_active(VT100Screen *vt);
static int vt100_screen_loc_is_between(
    VT100Screen *vt, struct vt100_loc loc,
    struct vt100_loc start, struct vt100_loc end);
static int vt100_screen_row_max_col(VT100Screen *vt, int row);

VT100Screen *vt100_screen_new()
{
    VT100Screen *vt;

    vt = calloc(1, sizeof(VT100Screen));
    vt100_screen_init(vt);

    return vt;
}

void vt100_screen_init(VT100Screen *vt)
{
    vt->grid = calloc(1, sizeof(struct vt100_grid));
    vt100_parser_yylex_init_extra(vt, &vt->scanner);
}

void vt100_screen_set_window_size(VT100Screen *vt)
{
    struct vt100_loc old_size;
    int i;

    old_size.row = vt->grid->max.row;
    old_size.col = vt->grid->max.col;

    /* vt->grid->max.row = vt->display.ypixel / vt->display.fonty; */
    /* vt->grid->max.col = vt->display.xpixel / vt->display.fontx; */
    // XXX vt100
    vt->grid->max.row = 24;
    vt->grid->max.col = 80;

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

int vt100_screen_process_string(VT100Screen *vt, char *buf, size_t len)
{
    int remaining;

    vt->state = vt100_parser_yy_scan_bytes(buf, len, vt->scanner);
    remaining = vt100_parser_yylex(vt->scanner);
    vt100_parser_yy_delete_buffer(vt->state, vt->scanner);
    return len - remaining;
}

int vt100_screen_loc_is_selected(VT100Screen *vt, struct vt100_loc loc)
{
    struct vt100_loc start = vt->grid->selection_start;
    struct vt100_loc end = vt->grid->selection_end;

    if (!vt->has_selection) {
        return 0;
    }

    if (loc.row == start.row) {
        int start_max_col;

        start_max_col = vt100_screen_row_max_col(vt, start.row);
        if (start.col > start_max_col) {
            start.col = vt->grid->max.col;
        }
    }

    if (loc.row == end.row) {
        int end_max_col;

        end_max_col = vt100_screen_row_max_col(vt, end.row);
        if (end.col > end_max_col) {
            end.col = vt->grid->max.col;
        }
    }

    return vt100_screen_loc_is_between(vt, loc, start, end);
}

void vt100_screen_get_string(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp)
{
    int row, col;
    size_t capacity = 8;

    *lenp = 0;

    if (end->row < start->row || (end->row == start->row && end->col <= start->col)) {
        return;
    }

    *strp = malloc(capacity);

    for (row = start->row; row <= end->row; ++row) {
        int start_col, end_col, max_col;
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

            if (cell->len == 0) {
                contents = " ";
                len = 1;
            }

            if (*lenp + len > capacity) {
                capacity *= 1.5;
                *strp = realloc(*strp, capacity);
            }
            memcpy(*strp + *lenp, contents, len);
            *lenp += len;
        }

        if ((row != end->row || end->col > max_col) && !grid_row->wrapped) {
            if (*lenp + 1 > capacity) {
                capacity *= 1.5;
                *strp = realloc(*strp, capacity);
            }
            memcpy(*strp + *lenp, "\n", 1);
            *lenp += 1;
        }
    }
}

struct vt100_cell *vt100_screen_get_cell(VT100Screen *vt, int row, int col)
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
    int col = vt->grid->cur.col;

    for (i = 0; i < len; ++i) {
        struct vt100_cell *cell;

        if (col >= vt->grid->max.col) {
            vt100_screen_row_at(vt, vt->grid->cur.row)->wrapped = 1;
            vt100_screen_move_to(vt, vt->grid->cur.row + 1, 0);
            col = 0;
        }
        cell = vt100_screen_cell_at(vt, vt->grid->cur.row, col++);

        cell->len = 1;
        cell->contents[0] = buf[i];
        cell->attrs = vt->attrs;
        cell->is_wide = 0;
    }
    vt100_screen_move_to(vt, vt->grid->cur.row, col);

    vt->dirty = 1;
}

void vt100_screen_show_string_utf8(VT100Screen *vt, char *buf, size_t len)
{
    char *c = buf, *next;
    int col = vt->grid->cur.col;

    /* XXX need to detect combining characters and append them to the previous
     * cell */
    while ((next = g_utf8_next_char(c))) {
        gunichar uc;
        struct vt100_cell *cell = NULL;
        int is_wide, is_combining;
        GUnicodeType ctype;

        uc = g_utf8_get_char(c);
        /* XXX handle zero width characters */
        is_wide = g_unichar_iswide(uc);
        ctype = g_unichar_type(uc);
        /* XXX should this also include spacing marks? */
        is_combining = ctype == G_UNICODE_ENCLOSING_MARK
                    || ctype == G_UNICODE_NON_SPACING_MARK;

        if (is_combining) {
            if (col > 0) {
                cell = vt100_screen_cell_at(
                    vt, vt->grid->cur.row, col - 1);
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
                memcpy(cell->contents, normal, cell->len);
                free(normal);
            }
        }
        else {
            if (col + (is_wide ? 2 : 1) > vt->grid->max.col) {
                vt100_screen_row_at(vt, vt->grid->cur.row)->wrapped = 1;
                vt100_screen_move_to(vt, vt->grid->cur.row + 1, 0);
                col = 0;
            }
            cell = vt100_screen_cell_at(vt, vt->grid->cur.row, col);
            cell->is_wide = is_wide;

            cell->len = next - c;
            memcpy(cell->contents, c, cell->len);
            cell->attrs = vt->attrs;

            col += is_wide ? 2 : 1;
        }

        c = next;
        if ((size_t)(c - buf) >= len) {
            break;
        }
    }
    vt100_screen_move_to(vt, vt->grid->cur.row, col);

    vt->dirty = 1;
}

void vt100_screen_move_to(VT100Screen *vt, int row, int col)
{
    int top = vt->grid->scroll_top, bottom = vt->grid->scroll_bottom;

    if (row > bottom) {
        vt100_screen_scroll_down(vt, row - bottom);
        row = bottom;
    }
    else if (row < top) {
        vt100_screen_scroll_up(vt, top - row);
        row = top;
    }

    if (col < 0) {
        col = 0;
    }

    if (col > vt->grid->max.col) {
        col = vt->grid->max.col;
    }

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

    vt100_screen_move_to(vt, vt->grid->scroll_top, 0);
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
    vt100_screen_set_window_size(vt);

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

    vt100_screen_set_window_size(vt);

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

    vt100_parser_yylex_destroy(vt->scanner);
}

void vt100_screen_delete(VT100Screen *vt)
{
    vt100_screen_cleanup(vt);
    free(vt);
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

static struct vt100_cell *vt100_screen_cell_at(VT100Screen *vt, int row, int col)
{
    return &vt->grid->rows[row + vt->grid->row_top].cells[col];
}

static void vt100_screen_scroll_down(VT100Screen *vt, int count)
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
        /* int scrollback = vt->config.scrollback_length; */
        int scrollback = 4096; // XXX vt100

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

static void vt100_screen_scroll_up(VT100Screen *vt, int count)
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

static int vt100_screen_scroll_region_is_active(VT100Screen *vt)
{
    return vt->grid->scroll_top != 0
        || vt->grid->scroll_bottom != vt->grid->max.row - 1;
}

static int vt100_screen_loc_is_between(
    VT100Screen *vt, struct vt100_loc loc,
    struct vt100_loc start, struct vt100_loc end)
{
    UNUSED(vt);

    if (end.row < start.row || (end.row == start.row && end.col < start.col)) {
        struct vt100_loc tmp;

        tmp = start;
        start = end;
        end = tmp;
    }

    if (loc.row < start.row || loc.row > end.row) {
        return 0;
    }

    if (loc.row == start.row && loc.col < start.col) {
        return 0;
    }

    if (loc.row == end.row && loc.col >= end.col) {
        return 0;
    }

    return 1;
}

static int vt100_screen_row_max_col(VT100Screen *vt, int row)
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
