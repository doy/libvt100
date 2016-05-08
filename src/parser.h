#ifndef _VT100_PARSER_H
#define _VT100_PARSER_H

#include <stdint.h>

int vt100_parser_yylex(VT100Screen *vt, uint8_t *yytext, size_t yyleng);

#endif
