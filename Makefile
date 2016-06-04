OUT      = libvt100.so
SOUT     = libvt100.a
BUILD    = build/
SRC      = src/
OBJ      = $(BUILD)parser.o \
	   $(BUILD)screen.o \
	   $(BUILD)unicode-extra.o
LIBS     = glib-2.0
OPT     ?= -g
CFLAGS  ?= $(OPT) -Wall -Wextra -Werror -pedantic -std=c1x -D_XOPEN_SOURCE=600
LDFLAGS ?= $(OPT)

ALLCFLAGS  = $(shell pkg-config --cflags $(LIBS)) $(CFLAGS)
ALLLDFLAGS = $(shell pkg-config --libs $(LIBS)) $(LDFLAGS)

MAKEDEPEND = $(CC) $(ALLCFLAGS) -M -MP -MT '$@ $(@:$(BUILD)%.o=$(BUILD).%.d)'

ifndef VERBOSE
QUIET_CC  = @echo "  CC  $@";
QUIET_LD  = @echo "  LD  $@";
QUIET_AR  = @echo "  AR  $@";
QUIET_LEX = @echo "  LEX $@";
endif

all: $(OUT) $(SOUT)

build: $(OUT)

static: $(SOUT)

$(OUT): $(OBJ)
	$(QUIET_LD)$(CC) -fPIC -shared -o $@ $^ $(ALLLDFLAGS)

$(SOUT): $(OBJ)
	$(QUIET_AR)$(AR) rcs $@ $^

$(BUILD)%.o: $(SRC)%.c | $(BUILD)
	@$(MAKEDEPEND) -o $(<:$(SRC)%.c=$(BUILD).%.d) $<
	$(QUIET_CC)$(CC) $(ALLCFLAGS) -c -fPIC -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

$(SRC)screen.c: $(SRC)parser.h

$(SRC)%.c: $(SRC)%.l
	$(QUIET_LEX)$(LEX) -o $@ $<

$(SRC)%.h: $(SRC)%.l
	$(QUIET_LEX)$(LEX) --header-file=$(<:.l=.h) -o /dev/null $<

clean:
	rm -f $(OUT) $(SOUT) $(OBJ) $(OBJ:$(BUILD)%.o=$(BUILD).%.d)
	@rmdir -p $(BUILD) > /dev/null 2>&1 || true

-include $(OBJ:$(BUILD)%.o=$(BUILD).%.d)

.PHONY: build clean
