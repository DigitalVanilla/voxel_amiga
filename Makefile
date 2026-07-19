################################################################################
# Voxel Amiga demo - SAGE + Amiga GCC
################################################################################

GCC_ROOT ?= D:/SDK/amiga-gcc
GCC_TARGET = $(GCC_ROOT)/m68k-amigaos

CC = "$(GCC_ROOT)/bin/m68k-amigaos-gcc.exe"
AS = "$(GCC_ROOT)/bin/vasmm68k_mot.exe"
MKDIR_P = mkdir -p
HOST_CC = D:/SDK/w64devkit/bin/gcc.exe

export PATH := $(GCC_ROOT)/bin;$(PATH)

APP_CSRCS = voxel.c voxel_log.c voxel_math.c voxel_renderer.c voxel_sage_support.c
SAGE_DIR = include/SAGE
SAGE_SRC_DIR = $(SAGE_DIR)/src
OBJ_DIR = build/obj
BIN_DIR = build/bin
PACKAGE_DIR = build/package/voxel_amiga
TARGET = $(BIN_DIR)/voxel_amiga
HOST_DIR = build/host
HOST_RENDERER_TEST = $(HOST_DIR)/voxel_renderer_compare.exe
HOST_AMMX_RENDERER_OBJ = $(HOST_DIR)/voxel_renderer_ammx.o
VOXEL_AMMX_COBJ = $(OBJ_DIR)/voxel_renderer_ammx_080.o
VOXEL_AMMX_ASMOBJ = $(OBJ_DIR)/voxel_renderer_ammx.o
MAP_RUNTIME_FILES = \
	maps/map0.height.raw \
	maps/map0.color.raw \
	maps/map0.palette.raw

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
OBJS = $(APP_OBJS) $(VOXEL_AMMX_COBJ) \
	$(VOXEL_AMMX_ASMOBJ) \
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
RENDERER_AMMX_CFLAGS = $(RENDERER_CFLAGS) \
	-DVOXEL_RENDERER_AMMX_BUILD=1 \
	-DVoxelRenderC=VoxelRenderC080AMMXUnused \
	-DVoxelRenderFastC=VoxelRenderFastAMMX

# GCC 6.5.0b crashes internally on sage_screen.c at -O2.
SCREEN_CFLAGS = $(filter-out -O2,$(CFLAGS)) -O0
ASFLAGS = -Fhunk -m68060
AMMX_ASFLAGS = -Fhunk -m68080
LDFLAGS = $(CPUFLAGS) -noixemul
LDLIBS = -lm -lamiga

.PHONY: all clean rebuild package convert-maps test-renderer

all: package

$(OBJ_DIR) $(BIN_DIR) $(PACKAGE_DIR) $(HOST_DIR):
	$(MKDIR_P) $@

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(OBJ_DIR)/voxel.o: voxel_renderer.h

$(OBJ_DIR)/voxel_renderer.o: voxel_renderer.c voxel_renderer.h | $(OBJ_DIR)
	$(CC) $(RENDERER_CFLAGS) -c $< -o $@

$(VOXEL_AMMX_COBJ): voxel_renderer.c voxel_renderer.h | $(OBJ_DIR)
	$(CC) $(RENDERER_AMMX_CFLAGS) -c $< -o $@

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

package: $(TARGET) $(MAP_RUNTIME_FILES)
	rm -rf $(PACKAGE_DIR)
	$(MKDIR_P) $(PACKAGE_DIR)/maps
	cp -f $(TARGET) $(PACKAGE_DIR)/
	cp -f $(MAP_RUNTIME_FILES) $(PACKAGE_DIR)/maps/

convert-maps:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/convert_maps.ps1

$(HOST_AMMX_RENDERER_OBJ): voxel_renderer.c voxel_renderer.h | $(HOST_DIR)
	$(HOST_CC) -O2 -std=gnu11 -fno-fast-math -Wall -Wextra \
		-Wno-implicit-fallthrough \
		-DVOXEL_RENDERER_AMMX_BUILD=1 -DVOXEL_AMMX_HOST_TEST=1 \
		-DVoxelRenderC=VoxelRenderCColumnUnused \
		-DVoxelRenderFastC=VoxelRenderFastAMMX \
		-I. -I$(SAGE_DIR)/include -I$(GCC_TARGET)/ndk-include \
		-idirafter include/LibInclude -c voxel_renderer.c -o $@

$(HOST_RENDERER_TEST): tests/voxel_renderer_compare.c voxel_renderer.c \
		voxel_renderer.h $(HOST_AMMX_RENDERER_OBJ) | $(HOST_DIR)
	$(HOST_CC) -O2 -std=gnu11 -fno-fast-math -Wall -Wextra \
		-Wno-implicit-fallthrough \
		-I. -I$(SAGE_DIR)/include -I$(GCC_TARGET)/ndk-include \
		-idirafter include/LibInclude \
		tests/voxel_renderer_compare.c voxel_renderer.c \
		$(HOST_AMMX_RENDERER_OBJ) -o $@

test-renderer: $(HOST_RENDERER_TEST)
	$(HOST_RENDERER_TEST)

clean:
	rm -rf build

rebuild: clean
	$(MAKE) all
