OUT      = libvt100.so
SOUT     = libvt100.a
BUILD    = build/
SRC      = src/
OBJ      = $(BUILD)parser.o \
	   $(BUILD)screen.o \
	   $(BUILD)unicode-extra.o
LIBS     = glib-2.0
OPT     ?= -g
CFLAGS  ?= $(OPT) -Wall -Wextra -Werror
LDFLAGS ?= $(OPT) -Wall -Wextra -Werror

RE2C      ?= re2c
RE2CFLAGS ?= -s --no-generation-date

ALLCFLAGS  = $(shell pkg-config --cflags $(LIBS)) $(CFLAGS)
ALLLDFLAGS = $(shell pkg-config --libs $(LIBS)) $(LDFLAGS)

MAKEDEPEND = $(CC) $(ALLCFLAGS) -M -MP -MT '$@ $(@:$(BUILD)%.o=$(BUILD).%.d)'

all: $(OUT) $(SOUT)

build: $(OUT)

static: $(SOUT)

$(OUT): $(OBJ)
	$(CC) -fPIC -shared -o $@ $^ $(ALLLDFLAGS)

$(SOUT): $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)%.o: $(SRC)%.c
	@mkdir -p $(BUILD)
	@$(MAKEDEPEND) -o $(<:$(SRC)%.c=$(BUILD).%.d) $<
	$(CC) $(ALLCFLAGS) -c -fPIC -o $@ $<

$(SRC)screen.c: $(SRC)parser.h

$(SRC)%.c: $(SRC)%.re
	$(RE2C) $(RE2CFLAGS) -o $@ $<

clean:
	rm -f $(OUT) $(SOUT) $(OBJ) $(OBJ:$(BUILD)%.o=$(BUILD).%.d)
	@rmdir -p $(BUILD) > /dev/null 2>&1 || true

-include $(OBJ:$(BUILD)%.o=$(BUILD).%.d)

.PHONY: build clean
