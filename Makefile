CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -fPIC -std=c11 -Iinclude
WIN_CC ?= x86_64-w64-mingw32-gcc

OUT_DIR = out
LIB_LINUX = $(OUT_DIR)/shabbat_plugin.so
LIB_WINDOWS = $(OUT_DIR)/shabbat_plugin.dll

SRC = src/shabbat_plugin.c

all: linux

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

linux: $(OUT_DIR)
	$(CC) $(CFLAGS) -shared $(SRC) -o $(LIB_LINUX) -lpthread -lX11
	@echo "Linux plugin built: $(LIB_LINUX)"

windows: $(OUT_DIR)
	$(WIN_CC) $(CFLAGS) -shared $(SRC) -o $(LIB_WINDOWS) -lgdi32 -luser32
	@echo "Windows plugin built: $(LIB_WINDOWS)"

both: linux windows

clean:
	rm -rf $(OUT_DIR)

.PHONY: all linux windows both clean
