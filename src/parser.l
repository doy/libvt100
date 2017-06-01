%{
#include <string.h>

#include "vt100.h"

#define UNUSED(x) ((void)x)

#define VT100_PARSER_CSI_MAX_PARAMS 256

#define YY_EXIT_FAILURE (UNUSED(yyscanner), 2)
%}

%option reentrant nodefault batch
%option noyywrap nounput noinput noyyalloc noyyrealloc noyyfree
%option prefix="vt100_parser_yy"
%option extra-type="VT100Screen *"

CTRL    [\000-\037\177]
ASCII   [\040-\176]
LEAD2   [\300-\337]
LEAD3   [\340-\357]
LEAD4   [\360-\367]
CONT    [\200-\277]
UNICHAR ({LEAD2}{CONT}|{LEAD3}{CONT}{CONT}|{LEAD4}{CONT}{CONT}{CONT})
CHAR    ({ASCII}|{UNICHAR})

ST  \007
BEL \007
BS  \010
TAB \011
LF  \012
VT  \013
FF  \014
CR  \015
SI  \017
ESC \033

DECKPAM {ESC}=
DECKPNM {ESC}>
CSI     {ESC}\[
OSC     {ESC}\]
RI      {ESC}M
RIS     {ESC}c
VB      {ESC}g
DECSC   {ESC}7
DECRC   {ESC}8

DECCSI     {CSI}\?
STATUSCSI  {CSI}>
CSIPARAM1  ([0-9]+)?
CSIPARAM2  ([0-9]+(;[0-9]+)?)?
CSIPARAM24 ([0-9]+(;[0-9]+){1,3})?
CSIPARAMS  ([0-9]+(;[0-9]+)*)?
SMPARAMS   ([<=?]?[0-9]+(;[<=?]?[0-9]+)*)?

ICH {CSI}{CSIPARAM1}@
CUU {CSI}{CSIPARAM1}A
CUD {CSI}{CSIPARAM1}B
CUF {CSI}{CSIPARAM1}C
CUB {CSI}{CSIPARAM1}D
CHA {CSI}{CSIPARAM1}G
CUP {CSI}{CSIPARAM2}H
ED  {CSI}{CSIPARAM1}J
EL  {CSI}{CSIPARAM1}K
IL  {CSI}{CSIPARAM1}L
DL  {CSI}{CSIPARAM1}M
DCH {CSI}{CSIPARAM1}P
SU  {CSI}{CSIPARAM1}S
SD  {CSI}{CSIPARAM1}T
ECH {CSI}{CSIPARAM1}X
VPA {CSI}{CSIPARAM1}d
SM  {CSI}{SMPARAMS}h
RM  {CSI}{SMPARAMS}l
SGR {CSI}{CSIPARAMS}m
CSR {CSI}{CSIPARAM24}r

DECSED {DECCSI}{CSIPARAM1}J
DECSEL {DECCSI}{CSIPARAM1}K

OSC0 {OSC}0;{CHAR}*{ST}
OSC1 {OSC}1;{CHAR}*{ST}
OSC2 {OSC}2;{CHAR}*{ST}

GZD4 {ESC}\([\040-\057]*[\060-\176]
G1D4 {ESC}\)[\040-\057]*[\060-\176]
G2D4 {ESC}\*[\040-\057]*[\060-\176]
G3D4 {ESC}\+[\040-\057]*[\060-\176]

DA  {STATUSCSI}{CSIPARAM1}c
DSR {CSI}{CSIPARAM1}n

%{
static void vt100_parser_handle_bel(VT100Screen *vt);
static void vt100_parser_handle_bs(VT100Screen *vt);
static void vt100_parser_handle_tab(VT100Screen *vt);
static void vt100_parser_handle_lf(VT100Screen *vt);
static void vt100_parser_handle_cr(VT100Screen *vt);
static void vt100_parser_handle_deckpam(VT100Screen *vt);
static void vt100_parser_handle_deckpnm(VT100Screen *vt);
static void vt100_parser_handle_ri(VT100Screen *vt);
static void vt100_parser_handle_ris(VT100Screen *vt);
static void vt100_parser_handle_vb(VT100Screen *vt);
static void vt100_parser_handle_decsc(VT100Screen *vt);
static void vt100_parser_handle_decrc(VT100Screen *vt);
static void vt100_parser_extract_csi_params(
    char *buf, size_t len, int *params, int *nparams);
static void vt100_parser_extract_sm_params(
    char *buf, size_t len, char *modes, int *params, int *nparams);
static void vt100_parser_handle_ich(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_cuu(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_cud(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_cuf(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_cub(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_cha(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_cup(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_ed(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_el(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_il(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_dl(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_dch(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_su(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_sd(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_ech(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_vpa(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_sm(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_rm(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_sgr(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_csr(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_decsed(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_decsel(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_osc0(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_osc1(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_osc2(VT100Screen *vt, char *buf, size_t len);
static void vt100_parser_handle_ascii(VT100Screen *vt, char *text, size_t len);
static void vt100_parser_handle_text(VT100Screen *vt, char *text, size_t len);
%}

%%

{BEL}     vt100_parser_handle_bel(yyextra);
{BS}      vt100_parser_handle_bs(yyextra);
{TAB}     vt100_parser_handle_tab(yyextra);
{LF}      |
{VT}      |
{FF}      vt100_parser_handle_lf(yyextra);
{CR}      vt100_parser_handle_cr(yyextra);
{SI}      /* ignored */

{DECKPAM} vt100_parser_handle_deckpam(yyextra);
{DECKPNM} vt100_parser_handle_deckpnm(yyextra);
{RI}      vt100_parser_handle_ri(yyextra);
{RIS}     vt100_parser_handle_ris(yyextra);
{VB}      vt100_parser_handle_vb(yyextra);
{DECSC}   vt100_parser_handle_decsc(yyextra);
{DECRC}   vt100_parser_handle_decrc(yyextra);

{ICH}     vt100_parser_handle_ich(yyextra, yytext, yyleng);
{CUU}     vt100_parser_handle_cuu(yyextra, yytext, yyleng);
{CUD}     vt100_parser_handle_cud(yyextra, yytext, yyleng);
{CUF}     vt100_parser_handle_cuf(yyextra, yytext, yyleng);
{CUB}     vt100_parser_handle_cub(yyextra, yytext, yyleng);
{CHA}     vt100_parser_handle_cha(yyextra, yytext, yyleng);
{CUP}     vt100_parser_handle_cup(yyextra, yytext, yyleng);
{ED}      vt100_parser_handle_ed(yyextra, yytext, yyleng);
{EL}      vt100_parser_handle_el(yyextra, yytext, yyleng);
{IL}      vt100_parser_handle_il(yyextra, yytext, yyleng);
{DL}      vt100_parser_handle_dl(yyextra, yytext, yyleng);
{DCH}     vt100_parser_handle_dch(yyextra, yytext, yyleng);
{SU}      vt100_parser_handle_su(yyextra, yytext, yyleng);
{SD}      vt100_parser_handle_sd(yyextra, yytext, yyleng);
{ECH}     vt100_parser_handle_ech(yyextra, yytext, yyleng);
{VPA}     vt100_parser_handle_vpa(yyextra, yytext, yyleng);
{SM}      vt100_parser_handle_sm(yyextra, yytext, yyleng);
{RM}      vt100_parser_handle_rm(yyextra, yytext, yyleng);
{SGR}     vt100_parser_handle_sgr(yyextra, yytext, yyleng);
{CSR}     vt100_parser_handle_csr(yyextra, yytext, yyleng);

{DECSED}  vt100_parser_handle_decsed(yyextra, yytext, yyleng);
{DECSEL}  vt100_parser_handle_decsel(yyextra, yytext, yyleng);

{OSC0}    vt100_parser_handle_osc0(yyextra, yytext, yyleng);
{OSC1}    vt100_parser_handle_osc1(yyextra, yytext, yyleng);
{OSC2}    vt100_parser_handle_osc2(yyextra, yytext, yyleng);

{GZD4}    |
{G1D4}    |
{G2D4}    |
{G3D4}    /* ignored - not interested in implementing character sets, unicode
             should be sufficient */

{DA}      |
{DSR}     /* ignored - not interested in escapes that generate responses */

{ASCII}+  vt100_parser_handle_ascii(yyextra, yytext, yyleng);
{CHAR}+   vt100_parser_handle_text(yyextra, yytext, yyleng);

{LEAD2}                       |
{LEAD3}{CONT}?                |
{LEAD4}{CONT}?{CONT}?         |
{CSI}[<=?]?{CSIPARAMS}[0-9;]? |
{OSC}{CHAR}*                  |
{ESC}                         return yyleng;

<<EOF>> return 0;

{CSI}[<=?]?{CSIPARAMS}{CTRL} {
    fprintf(stderr,
        "unhandled CSI sequence: \\033%s\\%03hho\n",
        yytext + 1, yytext[yyleng - 1]);
}

{CSI}[<=?]?{CSIPARAMS}{CHAR} {
    fprintf(stderr, "unhandled CSI sequence: \\033%s\n", yytext + 1);
}

{OSC}{CHAR}*{ST} {
    if (!strncmp(yytext, "\033]50;", 5)) { // osx terminal.app private stuff
        // not interested in non-portable extensions
    }
    else if (!strncmp(yytext, "\033]499;", 5)) { // termcast private metadata
        // this isn't intended to be interpreted
    }
    else {
        yytext[yyleng - 1] = '\0';
        fprintf(stderr,
            "unhandled OSC sequence: \\033%s\\007\n",
            yytext + 1);
    }
}

{ESC}{CTRL} {
    fprintf(stderr, "unhandled escape sequence: \\%03hho\n", yytext[1]);
}

{ESC}{CHAR} {
    switch (yytext[1]) {
    case '(': // character sets - there should be some trailing bytes
        return yyleng;
    default:
        fprintf(stderr, "unhandled escape sequence: %s\n", yytext + 1);
        break;
    }
}

{CTRL} {
    fprintf(stderr, "unhandled control character: \\%03hho\n", yytext[0]);
}

(?s:.) {
    fprintf(stderr, "invalid utf8 byte: \\%03hho\n", yytext[0]);
}

%%

#ifdef VT100_DEBUG_TRACE
#define DEBUG_TRACE1(x) do { \
    fputs(x"\n", stderr); \
} while (0)
#define DEBUG_TRACE3(x, x2, x2len) do { \
    char old = (x2)[(x2len)]; \
    (x2)[(x2len)] = '\0'; \
    fprintf(stderr, x" %s\n", (x2)); \
    (x2)[(x2len)] = old; \
} while (0)
#else
#define DEBUG_TRACE1(x)
#define DEBUG_TRACE3(x, x2, x2len)
#endif

static void vt100_parser_handle_bel(VT100Screen *vt)
{
    DEBUG_TRACE1("BEL");
    vt100_screen_audible_bell(vt);
}

static void vt100_parser_handle_bs(VT100Screen *vt)
{
    DEBUG_TRACE1("BS");
    vt100_screen_move_to(vt, vt->grid->cur.row, vt->grid->cur.col - 1);
}

static void vt100_parser_handle_tab(VT100Screen *vt)
{
    DEBUG_TRACE1("TAB");
    vt100_screen_move_to(
        vt, vt->grid->cur.row,
        vt->grid->cur.col - (vt->grid->cur.col % 8) + 8);
}

static void vt100_parser_handle_lf(VT100Screen *vt)
{
    DEBUG_TRACE1("LF");
    vt100_screen_move_down_or_scroll(vt);
}

static void vt100_parser_handle_cr(VT100Screen *vt)
{
    DEBUG_TRACE1("CR");
    vt100_screen_move_to(vt, vt->grid->cur.row, 0);
}

static void vt100_parser_handle_deckpam(VT100Screen *vt)
{
    DEBUG_TRACE1("DECKPAM");
    vt100_screen_set_application_keypad(vt);
}

static void vt100_parser_handle_deckpnm(VT100Screen *vt)
{
    DEBUG_TRACE1("DECKPNM");
    vt100_screen_reset_application_keypad(vt);
}

static void vt100_parser_handle_ri(VT100Screen *vt)
{
    DEBUG_TRACE1("RI");
    vt100_screen_move_up_or_scroll(vt);
}

static void vt100_parser_handle_ris(VT100Screen *vt)
{
    DEBUG_TRACE1("RIS");
    vt100_screen_use_normal_buffer(vt);
    vt100_screen_set_scroll_region(
        vt, 0, vt->grid->max.row - 1, 0, vt->grid->max.col - 1);
    vt100_screen_move_to(vt, 0, 0);
    vt100_screen_clear_screen(vt);
    vt100_screen_save_cursor(vt);
    vt100_screen_reset_text_attributes(vt);
    vt100_screen_show_cursor(vt);
    vt100_screen_set_mouse_reporting_normal_mode(vt);
    vt100_screen_reset_application_keypad(vt);
    vt100_screen_reset_application_cursor(vt);
    vt100_screen_reset_mouse_reporting_press(vt);
    vt100_screen_reset_mouse_reporting_press_release(vt);
    vt100_screen_reset_mouse_reporting_button_motion(vt);
    vt100_screen_reset_mouse_reporting_any_motion(vt);
    vt100_screen_reset_bracketed_paste(vt);
    vt100_screen_reset_origin_mode(vt);
}

static void vt100_parser_handle_vb(VT100Screen *vt)
{
    DEBUG_TRACE1("VB");
    vt100_screen_visual_bell(vt);
}

static void vt100_parser_handle_decsc(VT100Screen *vt)
{
    DEBUG_TRACE1("DECSC");
    vt100_screen_save_cursor(vt);
}

static void vt100_parser_handle_decrc(VT100Screen *vt)
{
    DEBUG_TRACE1("DECRC");
    vt100_screen_restore_cursor(vt);
}

static void vt100_parser_extract_csi_params(
    char *buf, size_t len, int *params, int *nparams)
{
    vt100_parser_extract_sm_params(buf, len, NULL, params, nparams);
}

static void vt100_parser_extract_sm_params(
    char *buf, size_t len, char *modes, int *params, int *nparams)
{
    char *pos = buf;

    /* this assumes that it will only ever be called on a fully matched CSI
     * sequence: accessing one character beyond the end is safe because CSI
     * sequences always have one character after the parameters (to determine
     * the type of sequence), and the parameters can only ever be digits,
     * separated by semicolons. */
    buf[len] = '\0';
    *nparams = 0;
    while ((size_t)(pos - buf) < len) {
        if (*nparams >= VT100_PARSER_CSI_MAX_PARAMS) {
            fprintf(stderr, "max CSI parameter length exceeded\n");
            break;
        }

        if (modes && (size_t)(pos - buf) < len) {
            if (strspn(pos, "0123456789")) {
                modes[*nparams] = '\0';
            }
            else {
                modes[*nparams] = *pos++;
            }
        }

        params[(*nparams)++] = atoi(pos);

        pos = strchr(pos, ';');
        if (pos) {
            pos++;
        }
        else {
            break;
        }
    }
}

static void vt100_parser_handle_ich(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("ICH", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_insert_characters(vt, params[0]);
}

static void vt100_parser_handle_cuu(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;
    int row = vt->grid->cur.row, new_row;

    DEBUG_TRACE3("CUU", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    new_row = row - params[0];
    if (row >= vt->grid->scroll_top && new_row < vt->grid->scroll_top) {
        new_row = vt->grid->scroll_top;
    }
    vt100_screen_move_to(vt, new_row, vt->grid->cur.col);
}

static void vt100_parser_handle_cud(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;
    int row = vt->grid->cur.row, new_row;

    DEBUG_TRACE3("CUD", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    new_row = row + params[0];
    if (row <= vt->grid->scroll_bottom && new_row > vt->grid->scroll_bottom) {
        new_row = vt->grid->scroll_bottom;
    }
    vt100_screen_move_to(vt, new_row, vt->grid->cur.col);
}

static void vt100_parser_handle_cuf(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("CUF", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_move_to(vt, vt->grid->cur.row, vt->grid->cur.col + params[0]);
}

static void vt100_parser_handle_cub(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("CUB", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_move_to(vt, vt->grid->cur.row, vt->grid->cur.col - params[0]);
}

static void vt100_parser_handle_cha(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("CHA", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_move_to(vt, vt->grid->cur.row, params[0] - 1);
}

static void vt100_parser_handle_cup(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 0, 0 }, nparams;

    DEBUG_TRACE3("CUP", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    if (params[0] == 0) {
        params[0] = 1;
    }
    if (params[1] == 0) {
        params[1] = 1;
    }
    vt100_screen_move_to(vt, params[0] - 1, params[1] - 1);
}

static void vt100_parser_handle_ed(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 0 }, nparams;

    /* this also gets called by handle_decsed, which will pass it something
     * of the form \e[?1J instead of \e[1J */
    buf += 2;
    len -= 3;
    if (*buf == '?') {
        buf++;
        len--;
        DEBUG_TRACE3("DECSED", buf, len);
    }
    else {
        DEBUG_TRACE3("ED", buf, len);
    }
    vt100_parser_extract_csi_params(buf, len, params, &nparams);
    switch (params[0]) {
    case 0:
        vt100_screen_clear_screen_forward(vt);
        break;
    case 1:
        vt100_screen_clear_screen_backward(vt);
        break;
    case 2:
        vt100_screen_clear_screen(vt);
        break;
    default:
        fprintf(stderr, "unknown ED parameter %d\n", params[0]);
        break;
    }
}

static void vt100_parser_handle_el(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 0 }, nparams;

    /* this also gets called by handle_decsel, which will pass it something
     * of the form \e[?1J instead of \e[1J */
    buf += 2;
    len -= 3;
    if (*buf == '?') {
        buf++;
        len--;
        DEBUG_TRACE3("DECSEL", buf, len);
    }
    else {
        DEBUG_TRACE3("EL", buf, len);
    }
    vt100_parser_extract_csi_params(buf, len, params, &nparams);
    switch (params[0]) {
    case 0:
        vt100_screen_kill_line_forward(vt);
        break;
    case 1:
        vt100_screen_kill_line_backward(vt);
        break;
    case 2:
        vt100_screen_kill_line(vt);
        break;
    default:
        fprintf(stderr, "unknown EL parameter %d\n", params[0]);
        break;
    }
}

static void vt100_parser_handle_il(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("IL", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_insert_lines(vt, params[0]);
}

static void vt100_parser_handle_dl(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("DL", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_delete_lines(vt, params[0]);
}

static void vt100_parser_handle_dch(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("DCH", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_delete_characters(vt, params[0]);
}

static void vt100_parser_handle_su(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("SU", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    if (params[0] == 0) {
        params[0] = 1;
    }
    vt100_screen_scroll_up(vt, params[0]);
}

static void vt100_parser_handle_sd(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("SD", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    if (params[0] == 0) {
        params[0] = 1;
    }
    vt100_screen_scroll_down(vt, params[0]);
}

static void vt100_parser_handle_ech(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("ECH", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_erase_characters(vt, params[0]);
}

static void vt100_parser_handle_vpa(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 1 }, nparams;

    DEBUG_TRACE3("VPA", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    vt100_screen_move_to(vt, params[0] - 1, vt->grid->cur.col);
}

static void vt100_parser_handle_sm(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS], nparams, i;
    char modes[VT100_PARSER_CSI_MAX_PARAMS] = { 0 };

    DEBUG_TRACE3("SM", buf + 2, len - 3);
    vt100_parser_extract_sm_params(buf + 2, len - 3, modes, params, &nparams);
    for (i = 0; i < nparams; ++i) {
        switch (modes[i]) {
        case 0:
            switch (params[i]) {
                case 34:
                    /* do nothing, no idea what this is even for */
                    break;
                default:
                    fprintf(stderr, "unknown SM parameter: %d\n", params[i]);
                    break;
            }
            break;
        case '?':
            switch (params[i]) {
            case 1:
                vt100_screen_set_application_cursor(vt);
                break;
            case 6:
                vt100_screen_set_origin_mode(vt);
                vt100_screen_move_to(vt, 0, 0);
                break;
            case 9:
                vt100_screen_set_mouse_reporting_press(vt);
                break;
            case 25:
                vt100_screen_show_cursor(vt);
                break;
            case 1000:
                vt100_screen_set_mouse_reporting_press_release(vt);
                break;
            case 1002:
                vt100_screen_set_mouse_reporting_button_motion(vt);
                break;
            case 1003:
                vt100_screen_set_mouse_reporting_any_motion(vt);
                break;
            case 1005:
                vt100_screen_set_mouse_reporting_utf8_mode(vt);
                break;
            case 1006:
                vt100_screen_set_mouse_reporting_sgr_mode(vt);
                break;
            case 47:
            case 1049:
                vt100_screen_use_alternate_buffer(vt);
                break;
            case 2004:
                vt100_screen_set_bracketed_paste(vt);
                break;
            case 12: // blinking cursor
                // not interested in blinking cursors
            case 1034: // interpret Meta key
                // not actually sure if ignoring this is correct - need to see
                // what exactly it does. don't think it's important though.
                break;
            default:
                fprintf(stderr,
                    "unknown SM parameter: %c%d\n", modes[i], params[i]);
                break;
            }
            break;
        default:
            fprintf(stderr,
                "unknown SM parameter: %c%d\n", modes[i], params[i]);
            break;
        }
    }
}

static void vt100_parser_handle_rm(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS], nparams, i;
    char modes[VT100_PARSER_CSI_MAX_PARAMS] = { 0 };

    DEBUG_TRACE3("RM", buf + 2, len - 3);
    vt100_parser_extract_sm_params(buf + 2, len - 3, modes, params, &nparams);
    for (i = 0; i < nparams; ++i) {
        switch (modes[i]) {
        case 0:
            switch (params[i]) {
                case 34:
                    /* do nothing, no idea what this is even for */
                    break;
                default:
                    fprintf(stderr, "unknown RM parameter: %d\n", params[i]);
                    break;
            }
            break;
        case '?':
            switch (params[i]) {
            case 1:
                vt100_screen_reset_application_cursor(vt);
                break;
            case 6:
                vt100_screen_reset_origin_mode(vt);
                vt100_screen_move_to(vt, 0, 0);
                break;
            case 9:
                vt100_screen_reset_mouse_reporting_press(vt);
                break;
            case 25:
                vt100_screen_hide_cursor(vt);
                break;
            case 1000:
                vt100_screen_reset_mouse_reporting_press_release(vt);
                break;
            case 1002:
                vt100_screen_reset_mouse_reporting_button_motion(vt);
                break;
            case 1003:
                vt100_screen_reset_mouse_reporting_any_motion(vt);
                break;
            case 1005:
            case 1006:
                vt100_screen_set_mouse_reporting_normal_mode(vt);
                break;
            case 47:
            case 1049:
                vt100_screen_use_normal_buffer(vt);
                break;
            case 2004:
                vt100_screen_reset_bracketed_paste(vt);
                break;
            case 12: // blinking cursor
                // not interested in blinking cursors
            case 1034: // interpret Meta key
                // not actually sure if ignoring this is correct - need to see
                // what exactly it does. don't think it's important though.
                break;
            default:
                fprintf(stderr,
                    "unknown RM parameter: %c%d\n", modes[i], params[i]);
                break;
            }
            break;
        default:
            fprintf(stderr,
                "unknown RM parameter: %c%d\n", modes[i], params[i]);
            break;
        }
    }
}

static void vt100_parser_handle_sgr(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = { 0 }, nparams, i;

    DEBUG_TRACE3("SGR", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);
    if (nparams < 1) {
        nparams = 1;
    }
    for (i = 0; i < nparams; ++i) {
        switch (params[i]) {
        case 0:
            vt100_screen_reset_text_attributes(vt);
            break;
        case 1:
            vt100_screen_set_bold(vt);
            break;
        case 3:
            vt100_screen_set_italic(vt);
            break;
        case 4:
            vt100_screen_set_underline(vt);
            break;
        case 7:
            vt100_screen_set_inverse(vt);
            break;
        case 22:
            vt100_screen_reset_bold(vt);
            break;
        case 23:
            vt100_screen_reset_italic(vt);
            break;
        case 24:
            vt100_screen_reset_underline(vt);
            break;
        case 27:
            vt100_screen_reset_inverse(vt);
            break;
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            vt100_screen_set_fg_color(vt, params[i] - 30);
            break;
        case 38: {
            i++;
            if (i >= nparams) {
                fprintf(stderr,
                    "unknown SGR parameter: %d (too few parameters)\n",
                    params[i - 1]);
                break;
            }

            switch (params[i]) {
            case 2:
                i += 3;
                if (i >= nparams) {
                    fprintf(stderr,
                        "unknown SGR parameter: %d;%d (too few parameters)\n",
                        params[i - 4], params[i - 3]);
                    break;
                }
                vt100_screen_set_fg_color_rgb(
                    vt, params[i - 2], params[i - 1], params[i]);
                break;
            case 5:
                i++;
                if (i >= nparams) {
                    fprintf(stderr,
                        "unknown SGR parameter: %d;%d (too few parameters)\n",
                        params[i - 2], params[i - 1]);
                    break;
                }
                vt100_screen_set_fg_color(vt, params[i]);
                break;
            default:
                i++;
                fprintf(stderr,
                    "unknown SGR parameter: %d;%d\n",
                    params[i - 2], params[i - 1]);
                break;
            }
            break;
        }
        case 39:
            vt100_screen_reset_fg_color(vt);
            break;
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            vt100_screen_set_bg_color(vt, params[i] - 40);
            break;
        case 48: {
            i++;
            if (i >= nparams) {
                fprintf(stderr,
                    "unknown SGR parameter: %d (too few parameters)\n",
                    params[i - 1]);
                break;
            }

            switch (params[i]) {
            case 2:
                i += 3;
                if (i >= nparams) {
                    fprintf(stderr,
                        "unknown SGR parameter: %d;%d (too few parameters)\n",
                        params[i - 4], params[i - 3]);
                    break;
                }
                vt100_screen_set_bg_color_rgb(
                    vt, params[i - 2], params[i - 1], params[i]);
                break;
            case 5:
                i++;
                if (i >= nparams) {
                    fprintf(stderr,
                        "unknown SGR parameter: %d;%d (too few parameters)\n",
                        params[i - 2], params[i - 1]);
                    break;
                }
                vt100_screen_set_bg_color(vt, params[i]);
                break;
            default:
                i++;
                fprintf(stderr,
                    "unknown SGR parameter: %d;%d\n",
                    params[i - 2], params[i - 1]);
                break;
            }
            break;
        }
        case 49:
            vt100_screen_reset_bg_color(vt);
            break;
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            vt100_screen_set_fg_color(vt, params[i] - 82);
            break;
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            vt100_screen_set_bg_color(vt, params[i] - 92);
            break;
        case 5: // blink mode
            // blinking terminals are awful
            break;
        default:
            fprintf(stderr, "unknown SGR parameter: %d\n", params[i]);
            break;
        }
    }
}

static void vt100_parser_handle_csr(VT100Screen *vt, char *buf, size_t len)
{
    int params[VT100_PARSER_CSI_MAX_PARAMS] = {
        1, vt->grid->max.row, 1, vt->grid->max.col };
    int nparams;

    DEBUG_TRACE3("CSR", buf + 2, len - 3);
    vt100_parser_extract_csi_params(buf + 2, len - 3, params, &nparams);

    vt100_screen_set_scroll_region(
        vt, params[0] - 1, params[1] - 1, params[2] - 1, params[3] - 1);
}

static void vt100_parser_handle_decsed(VT100Screen *vt, char *buf, size_t len)
{
    /* XXX not quite correct, but i don't think programs really use anything
     * that would show a difference */
    vt100_parser_handle_ed(vt, buf, len);
}

static void vt100_parser_handle_decsel(VT100Screen *vt, char *buf, size_t len)
{
    /* XXX not quite correct, but i don't think programs really use anything
     * that would show a difference */
    vt100_parser_handle_el(vt, buf, len);
}

static void vt100_parser_handle_osc0(VT100Screen *vt, char *buf, size_t len)
{
    DEBUG_TRACE3("OSC0", buf + 4, len - 5);
    vt100_screen_set_icon_name(vt, buf + 4, len - 5);
    vt100_screen_set_window_title(vt, buf + 4, len - 5);
}

static void vt100_parser_handle_osc1(VT100Screen *vt, char *buf, size_t len)
{
    DEBUG_TRACE3("OSC1", buf + 4, len - 5);
    vt100_screen_set_icon_name(vt, buf + 4, len - 5);
}

static void vt100_parser_handle_osc2(VT100Screen *vt, char *buf, size_t len)
{
    DEBUG_TRACE3("OSC2", buf + 4, len - 5);
    vt100_screen_set_window_title(vt, buf + 4, len - 5);
}

static void vt100_parser_handle_ascii(VT100Screen *vt, char *text, size_t len)
{
    DEBUG_TRACE3("TEXT", text, len);
    vt100_screen_show_string_ascii(vt, text, len);
}

static void vt100_parser_handle_text(VT100Screen *vt, char *text, size_t len)
{
    DEBUG_TRACE3("UTF8", text, len);
    vt100_screen_show_string_utf8(vt, text, len);
}

/* XXX these are copied from the generated file so that I can add the UNUSED
 * declarations, otherwise we get compilation errors */
void *vt100_parser_yyalloc(yy_size_t size, yyscan_t yyscanner)
{
    UNUSED(yyscanner);
    return (void *)malloc(size);
}

void *vt100_parser_yyrealloc(void *ptr, yy_size_t size, yyscan_t yyscanner)
{
    UNUSED(yyscanner);
    return (void *)realloc((char *)ptr, size);
}

void vt100_parser_yyfree(void *ptr, yyscan_t yyscanner)
{
    UNUSED(yyscanner);
    free((char *) ptr);
}
