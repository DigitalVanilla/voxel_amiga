################################################################################
# Voxel Amiga demo - SAGE + Amiga GCC
################################################################################

GCC_ROOT ?= D:/SDK/amiga-gcc
GCC_TARGET = $(GCC_ROOT)/m68k-amigaos

CC = "$(GCC_ROOT)/bin/m68k-amigaos-gcc.exe"
AS = "$(GCC_ROOT)/bin/vasmm68k_mot.exe"
MKDIR_P = mkdir -p

export PATH := $(GCC_ROOT)/bin;$(PATH)

APP_CSRCS = voxel.c voxel_log.c voxel_math.c voxel_renderer.c voxel_sage_support.c
SAGE_DIR = include/SAGE
SAGE_SRC_DIR = $(SAGE_DIR)/src
OBJ_DIR = build/obj
BIN_DIR = build/bin
PACKAGE_DIR = build/package/voxel_amiga
TARGET = $(BIN_DIR)/voxel_amiga
RELEASE_README = README_RELEASE.md
VOXEL_AMMX_ASMOBJ = $(OBJ_DIR)/voxel_renderer_ammx.o
MAP_INDEXES = 0 1 2 3 4 5 6 7 8 9 \
	10 11 12 13 14 15 16 17 18 19 \
	20 21 22 23 24 25 26 27 28 29
MAP_RUNTIME_FILES = $(foreach index,$(MAP_INDEXES),maps/map$(index).height.raw maps/map$(index).color.raw maps/map$(index).palette.raw)

SAGE_CSRCS = $(addprefix $(SAGE_SRC_DIR)/, \
	sage.c sage_logger.c sage_error.c sage_memory.c sage_thread.c \
	sage_maths.c sage_configfile.c sage_vampire.c sage_video.c \
	sage_bitmap.c sage_picture.c sage_event.c sage_screen.c sage_draw.c \
	sage_layer.c sage_sprite.c sage_tile.c sage_tilemap.c \
	sage_input.c sage_keyboard.c sage_joyport.c sage_timer.c \
	sage_interrupt.c)

SAGE_ASMSRCS = $(addprefix $(SAGE_SRC_DIR)/, \
	sage_blitter.asm sage_ammxblit.asm sage_fastdraw.asm \
	sage_vblint.asm sage_itserver.asm)

APP_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(APP_CSRCS))
SAGE_COBJS = $(patsubst $(SAGE_SRC_DIR)/%.c,$(OBJ_DIR)/sage_%.o,$(SAGE_CSRCS))
SAGE_ASMOBJS = $(patsubst $(SAGE_SRC_DIR)/%.asm,$(OBJ_DIR)/sage_%.o,$(SAGE_ASMSRCS))
OBJS = $(APP_OBJS) $(VOXEL_AMMX_ASMOBJ) \
	$(SAGE_COBJS) $(SAGE_ASMOBJS)

SAGE_DEBUG ?= 0
SAGE_SAFE ?= 0
CPUFLAGS = -m68060 -m68881
CFLAGS = $(CPUFLAGS) -O2 -std=gnu89 \
	-DPI=3.14159265358979323846 \
	-D_SAGE_DEBUG_MODE_=$(SAGE_DEBUG) \
	-D_SAGE_SAFE_MODE_=$(SAGE_SAFE) \
	-I. \
	-I$(SAGE_DIR)/include \
	-I$(GCC_TARGET)/ndk-include \
	-idirafter include/LibInclude

APP_CFLAGS = $(CFLAGS) -Wall -Wextra
RENDERER_CFLAGS = $(APP_CFLAGS) -fomit-frame-pointer

# GCC 6.5.0b crashes internally on sage_screen.c at -O2.
SCREEN_CFLAGS = $(filter-out -O2,$(CFLAGS)) -O0
ASFLAGS = -Fhunk -m68060
AMMX_ASFLAGS = -Fhunk -m68080
LDFLAGS = $(CPUFLAGS) -noixemul
LDLIBS = -lm -lamiga

.PHONY: all clean rebuild package convert-maps

all: package

$(OBJ_DIR) $(BIN_DIR) $(PACKAGE_DIR):
	$(MKDIR_P) $@

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(OBJ_DIR)/voxel.o: voxel_renderer.h

$(OBJ_DIR)/voxel_renderer.o: voxel_renderer.c voxel_renderer.h | $(OBJ_DIR)
	$(CC) $(RENDERER_CFLAGS) -c $< -o $@

$(VOXEL_AMMX_ASMOBJ): voxel_renderer_ammx.asm | $(OBJ_DIR)
	$(AS) $(AMMX_ASFLAGS) $< -o $@

$(OBJ_DIR)/voxel.o $(OBJ_DIR)/voxel_math.o: voxel_math.h

$(OBJ_DIR)/voxel.o $(OBJ_DIR)/voxel_log.o $(OBJ_DIR)/sage_sage_logger.o: voxel_log.h

$(OBJ_DIR)/sage_sage_screen.o: $(SAGE_SRC_DIR)/sage_screen.c | $(OBJ_DIR)
	$(CC) $(SCREEN_CFLAGS) -c $< -o $@

$(OBJ_DIR)/sage_%.o: $(SAGE_SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/sage_sage_ammxblit.o: $(SAGE_SRC_DIR)/sage_ammxblit.asm | $(OBJ_DIR)
	$(AS) $(AMMX_ASFLAGS) $< -o $@

$(OBJ_DIR)/sage_%.o: $(SAGE_SRC_DIR)/%.asm | $(OBJ_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

package: $(TARGET) $(MAP_RUNTIME_FILES) $(RELEASE_README)
	rm -rf $(PACKAGE_DIR)
	$(MKDIR_P) $(PACKAGE_DIR)/maps
	cp -f $(TARGET) $(PACKAGE_DIR)/
	cp -f $(RELEASE_README) $(PACKAGE_DIR)/README.md
	cp -f $(MAP_RUNTIME_FILES) $(PACKAGE_DIR)/maps/

convert-maps:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/convert_maps.ps1

clean:
	rm -rf build

rebuild: clean
	$(MAKE) all
