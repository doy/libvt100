#ifndef _VT100_SCREEN_H
#define _VT100_SCREEN_H

#include <stddef.h>
#include <stdint.h>

enum VT100ColorType {
    VT100_COLOR_DEFAULT,
    VT100_COLOR_IDX,
    VT100_COLOR_RGB
};

struct vt100_loc {
    int row;
    int col;
};

struct vt100_color {
    union {
        struct {
            union {
                struct {
                    unsigned char r;
                    unsigned char g;
                    unsigned char b;
                };
                unsigned char idx;
            };
            unsigned char type;
        };
        uint32_t id;
    };
};

struct vt100_cell_attrs {
    struct vt100_color fgcolor;
    struct vt100_color bgcolor;
    union {
        struct {
            unsigned int bold: 1;
            unsigned int italic: 1;
            unsigned int underline: 1;
            unsigned int inverse: 1;
        };
        unsigned char attrs;
    };
};

struct vt100_cell {
    char contents[8];
    size_t len;
    struct vt100_cell_attrs attrs;
    unsigned int is_wide: 1;
};

struct vt100_row {
    struct vt100_cell *cells;
    unsigned int wrapped: 1;
};

struct vt100_grid {
    struct vt100_loc cur;
    struct vt100_loc max;
    struct vt100_loc saved;

    int scroll_top;
    int scroll_bottom;

    int row_count;
    int row_capacity;
    int row_top;

    struct vt100_row *rows;
};

struct vt100_parser_state;
struct vt100_screen {
    struct vt100_grid *grid;
    struct vt100_grid *alternate;

    char *title;
    size_t title_len;
    char *icon_name;
    size_t icon_name_len;

    struct vt100_cell_attrs attrs;

    int scrollback_length;

    struct vt100_parser_state *parser_state;

    unsigned int hide_cursor: 1;
    unsigned int application_keypad: 1;
    unsigned int application_cursor: 1;
    unsigned int mouse_reporting_press: 1;
    unsigned int mouse_reporting_press_release: 1;
    unsigned int mouse_reporting_button_motion: 1;
    unsigned int mouse_reporting_sgr_mode: 1;
    unsigned int bracketed_paste: 1;

    unsigned int visual_bell: 1;
    unsigned int audible_bell: 1;
    unsigned int update_title: 1;
    unsigned int update_icon_name: 1;

    unsigned int dirty: 1;
    unsigned int custom_scrollback_length: 1;
};

VT100Screen *vt100_screen_new(int rows, int cols);
void vt100_screen_init(VT100Screen *vt);
void vt100_screen_set_window_size(VT100Screen *vt, int rows, int cols);
void vt100_screen_set_scrollback_length(VT100Screen *vt, int rows);
int vt100_screen_process_string(VT100Screen *vt, char *buf, size_t len);
void vt100_screen_get_string_plaintext(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp);
void vt100_screen_get_string_formatted(
    VT100Screen *vt, struct vt100_loc *start, struct vt100_loc *end,
    char **strp, size_t *lenp);
struct vt100_cell *vt100_screen_cell_at(VT100Screen *vt, int row, int col);
void vt100_screen_audible_bell(VT100Screen *vt);
void vt100_screen_visual_bell(VT100Screen *vt);
void vt100_screen_show_string_ascii(VT100Screen *vt, char *buf, size_t len);
void vt100_screen_show_string_utf8(VT100Screen *vt, char *buf, size_t len);
void vt100_screen_move_to(VT100Screen *vt, int row, int col);
void vt100_screen_clear_screen(VT100Screen *vt);
void vt100_screen_clear_screen_forward(VT100Screen *vt);
void vt100_screen_clear_screen_backward(VT100Screen *vt);
void vt100_screen_kill_line(VT100Screen *vt);
void vt100_screen_kill_line_forward(VT100Screen *vt);
void vt100_screen_kill_line_backward(VT100Screen *vt);
void vt100_screen_insert_characters(VT100Screen *vt, int count);
void vt100_screen_insert_lines(VT100Screen *vt, int count);
void vt100_screen_delete_characters(VT100Screen *vt, int count);
void vt100_screen_delete_lines(VT100Screen *vt, int count);
void vt100_screen_erase_characters(VT100Screen *vt, int count);
void vt100_screen_scroll_down(VT100Screen *vt, int count);
void vt100_screen_scroll_up(VT100Screen *vt, int count);
void vt100_screen_move_down_or_scroll(VT100Screen *vt);
void vt100_screen_move_up_or_scroll(VT100Screen *vt);
void vt100_screen_set_scroll_region(
    VT100Screen *vt, int top, int bottom, int left, int right);
void vt100_screen_reset_text_attributes(VT100Screen *vt);
void vt100_screen_set_fg_color(VT100Screen *vt, int idx);
void vt100_screen_set_fg_color_rgb(
    VT100Screen *vt, unsigned char r, unsigned char g, unsigned char b);
void vt100_screen_reset_fg_color(VT100Screen *vt);
void vt100_screen_set_bg_color(VT100Screen *vt, int idx);
void vt100_screen_set_bg_color_rgb(
    VT100Screen *vt, unsigned char r, unsigned char g, unsigned char b);
void vt100_screen_reset_bg_color(VT100Screen *vt);
void vt100_screen_set_bold(VT100Screen *vt);
void vt100_screen_set_italic(VT100Screen *vt);
void vt100_screen_set_underline(VT100Screen *vt);
void vt100_screen_set_inverse(VT100Screen *vt);
void vt100_screen_reset_bold(VT100Screen *vt);
void vt100_screen_reset_italic(VT100Screen *vt);
void vt100_screen_reset_underline(VT100Screen *vt);
void vt100_screen_reset_inverse(VT100Screen *vt);
void vt100_screen_use_alternate_buffer(VT100Screen *vt);
void vt100_screen_use_normal_buffer(VT100Screen *vt);
void vt100_screen_save_cursor(VT100Screen *vt);
void vt100_screen_restore_cursor(VT100Screen *vt);
void vt100_screen_show_cursor(VT100Screen *vt);
void vt100_screen_hide_cursor(VT100Screen *vt);
void vt100_screen_set_application_keypad(VT100Screen *vt);
void vt100_screen_reset_application_keypad(VT100Screen *vt);
void vt100_screen_set_application_cursor(VT100Screen *vt);
void vt100_screen_reset_application_cursor(VT100Screen *vt);
void vt100_screen_set_mouse_reporting_press(VT100Screen *vt);
void vt100_screen_reset_mouse_reporting_press(VT100Screen *vt);
void vt100_screen_set_mouse_reporting_press_release(VT100Screen *vt);
void vt100_screen_reset_mouse_reporting_press_release(VT100Screen *vt);
void vt100_screen_set_mouse_reporting_button_motion(VT100Screen *vt);
void vt100_screen_reset_mouse_reporting_button_motion(VT100Screen *vt);
void vt100_screen_set_mouse_reporting_sgr_mode(VT100Screen *vt);
void vt100_screen_reset_mouse_reporting_sgr_mode(VT100Screen *vt);
void vt100_screen_set_bracketed_paste(VT100Screen *vt);
void vt100_screen_reset_bracketed_paste(VT100Screen *vt);
void vt100_screen_set_window_title(VT100Screen *vt, char *buf, size_t len);
void vt100_screen_set_icon_name(VT100Screen *vt, char *buf, size_t len);
int vt100_screen_row_max_col(VT100Screen *vt, int row);
void vt100_screen_cleanup(VT100Screen *vt);
void vt100_screen_delete(VT100Screen *vt);

#endif
