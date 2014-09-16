from ctypes import *

libvt100 = CDLL("libvt100.so")

class vt100_loc(Structure):
    _fields_ = [
        ("row", c_int),
        ("col", c_int),
    ]

class vt100_rgb_color(Union):
    _fields_ = [
        ("r", c_ubyte),
        ("g", c_ubyte),
        ("b", c_ubyte),
    ]

class vt100_color(Structure):
    _anonymous_ = ("rgb",)
    _fields_ = [
        ("rgb", vt100_rgb_color),
        ("type", c_ubyte),
    ]

class vt100_named_attrs(Structure):
    _fields_ = [
        ("bold", c_ubyte, 1),
        ("italic", c_ubyte, 1),
        ("underline", c_ubyte, 1),
        ("inverse", c_ubyte, 1),
    ]

class vt100_attrs(Union):
    _anonymous_ = ("named",)
    _fields_ = [
        ("named", vt100_named_attrs),
        ("attrs", c_ubyte),
    ]

class vt100_cell_attrs(Structure):
    _anonymous_ = ("cell_attrs",)
    _fields_ = [
        ("fgcolor", vt100_color),
        ("bgcolor", vt100_color),
        ("cell_attrs", vt100_attrs),
    ]

class vt100_cell(Structure):
    _fields_ = [
        ("_contents", c_char * 8),
        ("len", c_size_t),
        ("attrs", vt100_cell_attrs),
        ("is_wide", c_ubyte, 1),
    ]

    def contents(self):
        return self._contents[:self.len].decode('utf-8')

new_prototype = CFUNCTYPE(c_void_p, c_int, c_int)
vt100_new = new_prototype(("vt100_screen_new", libvt100))

set_window_size_prototype = CFUNCTYPE(None, c_void_p, c_int, c_int)
vt100_set_window_size = set_window_size_prototype(("vt100_screen_set_window_size", libvt100))

process_string_prototype = CFUNCTYPE(c_int, c_void_p, c_char_p, c_size_t)
vt100_process_string = process_string_prototype(("vt100_screen_process_string", libvt100))

cell_at_prototype = CFUNCTYPE(POINTER(vt100_cell), c_void_p, c_int, c_int)
vt100_cell_at = cell_at_prototype(("vt100_screen_cell_at", libvt100))

get_string_formatted_prototype = CFUNCTYPE(None, c_void_p, POINTER(vt100_loc), POINTER(vt100_loc), POINTER(c_char_p), POINTER(c_size_t))
vt100_get_string_formatted = get_string_formatted_prototype(("vt100_screen_get_string_formatted", libvt100))

get_string_plaintext_prototype = CFUNCTYPE(None, c_void_p, POINTER(vt100_loc), POINTER(vt100_loc), POINTER(c_char_p), POINTER(c_size_t))
vt100_get_string_plaintext = get_string_plaintext_prototype(("vt100_screen_get_string_plaintext", libvt100))

delete_prototype = CFUNCTYPE(None, c_void_p)
vt100_delete = delete_prototype(("vt100_screen_delete", libvt100))

# XXX process/cell need mutexes
class vt100(object):
    def __init__(self, rows, cols):
        self.vt = vt100_new(rows, cols)

    def __del__(self):
        vt100_delete(self.vt)

    def set_window_size(self, rows, cols):
        vt100_set_window_size(self.vt, rows, cols)

    def process(self, string):
        return vt100_process_string(self.vt, string, len(string))

    def get_string_formatted(self, row_start, col_start, row_end, col_end):
        outstr = c_char_p()
        outlen = c_size_t()
        vt100_get_string_formatted(
            self.vt,
            byref(vt100_loc(row_start, col_start)),
            byref(vt100_loc(row_end, col_end)),
            byref(outstr),
            byref(outlen),
        )
        return string_at(outstr)[:outlen.value]

    def get_string_plaintext(self, row_start, col_start, row_end, col_end):
        outstr = c_char_p()
        outlen = c_size_t()
        vt100_get_string_plaintext(
            self.vt,
            byref(vt100_loc(row_start, col_start)),
            byref(vt100_loc(row_end, col_end)),
            byref(outstr),
            byref(outlen),
        )
        return string_at(outstr)[:outlen.value]

    def cell(self, x, y):
        return vt100_cell_at(self.vt, x, y).contents
