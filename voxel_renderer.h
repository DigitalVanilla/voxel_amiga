#ifndef VOXEL_RENDERER_H
#define VOXEL_RENDERER_H

#include <exec/types.h>
#include <sage/sage_bitmap.h>

#define VOXEL_HEIGHT_BOUND_DISABLED 256

typedef struct {
  SAGE_Bitmap *target;
  const UBYTE *height_map;
  const UBYTE *color_map;
  ULONG map_size;
  float camera_x;
  float camera_y;
  float camera_height;
  float camera_pitch;
  float camera_sine;
  float camera_cosine;
  float camera_roll;
  float base_vertical_offset;
  float roll_scale;
  float draw_distance;
  float height_scale;
  float depth_delta;
  int horizontal_divisions;
  UWORD height_map_max;
  UBYTE clear_color;
  UBYTE *column_buffer;
} voxel_render_params_t;

typedef struct {
  ULONG rays;
  ULONG depth_samples;
  ULONG spans;
  ULONG pixels;
} voxel_render_stats_t;

BOOL VoxelRenderC(const voxel_render_params_t *, voxel_render_stats_t *);
BOOL VoxelRenderFastC(const voxel_render_params_t *, voxel_render_stats_t *);
BOOL VoxelRenderFastAMMX(const voxel_render_params_t *, voxel_render_stats_t *);

#endif
