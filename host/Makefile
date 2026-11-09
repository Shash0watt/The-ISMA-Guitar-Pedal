# Mac test tools. Run from the repo root with:  make -C host
# (The Makefile in the repo root is ModusToolbox's firmware build; this one is
# separate.)

CC       = cc
FX_DIR   = ../api/effects
OUT      = build

# Same warning flags we want on the firmware side. -Wdouble-promotion catches
# accidental double math, which is slow on the XMC4700.
WARN     = -Wall -Wextra -Wshadow -Wdouble-promotion
# Address/undefined-behaviour sanitizers catch out-of-bounds buffer access
# (e.g. a delay line index going past the end) with a clear error message.
SANITIZE = -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS   = -std=c11 -O1 -g $(WARN) $(SANITIZE)

FX_SRC   = $(wildcard $(FX_DIR)/*.c)
FX_HDR   = $(wildcard $(FX_DIR)/*.h)

all: $(OUT)/fxrun $(OUT)/gen_test

$(OUT)/fxrun: fxrun.c fx_registry.c wav.c $(FX_SRC) fx_registry.h wav.h $(FX_HDR) | $(OUT)
	$(CC) $(CFLAGS) -I. -I$(FX_DIR) $(filter %.c,$^) -o $@ -lm

$(OUT)/gen_test: gen_test.c wav.c wav.h | $(OUT)
	$(CC) $(CFLAGS) -I. $(filter %.c,$^) -o $@ -lm

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
