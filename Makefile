SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin

ifeq '$(findstring ;,$(PATH))' ';'
UNAME := Windows
else
UNAME := $(shell uname 2>/dev/null || echo Unknown)
UNAME := $(patsubst CYGWIN%,Cygwin,$(UNAME))
UNAME := $(patsubst MSYS%,MSYS,$(UNAME))
UNAME := $(patsubst MINGW%,MSYS,$(UNAME))
endif

ifeq ($(UNAME),Windows)
RM := DEL /Q /F
RMDIR := RMDIR /Q /S
MKDIR := MKDIR
else
RM := rm -rfv
RMDIR := $(RM)
MKDIR := mkdir -p
endif

TARGETS := egl-khr-platform-gbm \
    triangle-rpi4 \
    linux-drm-opengles-robertkirkman \
    linux-drm-opengles-miouyouyou \
    drm-gbm \
    drm-triangle \
    egl-info

BINS := $(TARGETS:%=$(BIN_DIR)/%)

OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

CC ?= $(CROSS_COMPILE)gcc

CPPFLAGS += -MMD -MP -DEGL_EGLEXT_PROTOTYPES
CFLAGS ?= -Wall -g -O0
CFLAGS += $(shell pkg-config libdrm gbm --cflags) -I$(SRC_DIR)
LDFLAGS += $(shell pkg-config libdrm gbm --libs) -lEGL -lGLESv2 -lm

ifneq ($(GPU_PKG_CONFIG),)
CFLAGS += $(shell pkg-config $(GPU_PKG_CONFIG) --define-prefix=$(dir $(GPU_PKG_CONFIG))../../../ --cflags)
LDFLAGS += $(shell pkg-config $(GPU_PKG_CONFIG) --define-prefix=$(dir $(GPU_PKG_CONFIG))../../../ --libs)
endif

.PHONY: all clean $(TARGETS)

all: $(TARGETS)

$(TARGETS): %: $(BIN_DIR)/%

$(BINS): $(BIN_DIR)/%: $(OBJ_DIR)/%.o $(OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	$(MKDIR) $@

clean:
ifeq ($(UNAME),Windows)
	if exist $(BIN_DIR) $(RMDIR) $(BIN_DIR)
	if exist $(OBJ_DIR) $(RMDIR) $(OBJ_DIR)
else
	$(RMDIR) $(BIN_DIR)
	$(RMDIR) $(OBJ_DIR)
endif

install:
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN_DIR)/linux-drm-opengles-miouyouyou $(DESTDIR)$(PREFIX)/bin/linux-drm-opengles-miouyouyou
	install -m 0755 $(BIN_DIR)/linux-drm-opengles-robertkirkman $(DESTDIR)$(PREFIX)/bin/linux-drm-opengles-robertkirkman
	install -m 0755 $(BIN_DIR)/triangle-rpi4 $(DESTDIR)$(PREFIX)/bin/triangle-rpi4
	install -m 0755 $(BIN_DIR)/drm-gbm $(DESTDIR)$(PREFIX)/bin/drm-gbm
	install -m 0755 $(BIN_DIR)/drm-triangle $(DESTDIR)$(PREFIX)/bin/drm-triangle

-include $(OBJ:.o=.d)
