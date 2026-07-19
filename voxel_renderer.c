#include <string.h>
#include <stddef.h>

#include "voxel_renderer.h"

#if defined(VOXEL_RENDERER_AMMX_BUILD) && !defined(VOXEL_AMMX_HOST_TEST)
#define VOXEL_RENDERER_AMMX_TARGET 1
#endif

#ifdef VOXEL_RENDERER_AMMX_BUILD
#ifdef VOXEL_AMMX_HOST_TEST
static void transposeColumnBuffer(const UBYTE *source, UBYTE *destination,
                                  UWORD width, UWORD height, ULONG bpr)
{
  UWORD x;
  UWORD y;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++) {
      destination[(ULONG)y * bpr + x] =
        source[(ULONG)x * height + y];
    }
  }
}
#else
#include <sage/sage_compiler.h>

extern VOID ASM VoxelTransposeAMMX(
  REG(a0, const UBYTE *source),
  REG(a1, UBYTE *destination),
  REG(d0, UWORD width),
  REG(d1, UWORD height),
  REG(d2, ULONG destination_bpr)
);

typedef struct {
  const UBYTE *height_map;
  const UBYTE *color_map;
  const ULONG *depth_lut;
  const UWORD *stop_index;
  UBYTE *column_base;
  const LONG *advance_x;
  const LONG *advance_y;
  ULONG sample_x_phase;
  ULONG sample_y_phase;
  LONG camera_height;
  LONG camera_pitch_fixed;
  LONG column_offset;
  ULONG target_height;
  ULONG span_width;
} voxel_ammx_ray_t;

extern VOID ASM VoxelRenderRayAMMX(
  REG(a0, const voxel_ammx_ray_t *ray),
  REG(a1, voxel_render_stats_t *stats)
);

#define VOXEL_STATIC_ASSERT(name, condition) \
  typedef char name[(condition) ? 1 : -1]

VOXEL_STATIC_ASSERT(voxel_ammx_ray_height_map_offset,
                    offsetof(voxel_ammx_ray_t, height_map) == 0);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_color_map_offset,
                    offsetof(voxel_ammx_ray_t, color_map) == 4);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_depth_lut_offset,
                    offsetof(voxel_ammx_ray_t, depth_lut) == 8);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_stop_index_offset,
                    offsetof(voxel_ammx_ray_t, stop_index) == 12);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_column_base_offset,
                    offsetof(voxel_ammx_ray_t, column_base) == 16);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_advance_x_offset,
                    offsetof(voxel_ammx_ray_t, advance_x) == 20);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_advance_y_offset,
                    offsetof(voxel_ammx_ray_t, advance_y) == 24);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_sample_x_phase_offset,
                    offsetof(voxel_ammx_ray_t, sample_x_phase) == 28);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_sample_y_phase_offset,
                    offsetof(voxel_ammx_ray_t, sample_y_phase) == 32);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_camera_height_offset,
                    offsetof(voxel_ammx_ray_t, camera_height) == 36);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_camera_pitch_fixed_offset,
                    offsetof(voxel_ammx_ray_t, camera_pitch_fixed) == 40);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_column_offset_offset,
                    offsetof(voxel_ammx_ray_t, column_offset) == 44);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_target_height_offset,
                    offsetof(voxel_ammx_ray_t, target_height) == 48);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_span_width_offset,
                    offsetof(voxel_ammx_ray_t, span_width) == 52);
VOXEL_STATIC_ASSERT(voxel_ammx_ray_size,
                    sizeof(voxel_ammx_ray_t) == 56);
VOXEL_STATIC_ASSERT(voxel_render_stats_depth_samples_offset,
                    offsetof(voxel_render_stats_t, depth_samples) == 4);
VOXEL_STATIC_ASSERT(voxel_render_stats_spans_offset,
                    offsetof(voxel_render_stats_t, spans) == 8);
VOXEL_STATIC_ASSERT(voxel_render_stats_pixels_offset,
                    offsetof(voxel_render_stats_t, pixels) == 12);
#endif
#endif

#define VOXEL_FAST_DEPTH_CAPACITY 2048
#define VOXEL_FAST_MAP_SIZE       1024
#define VOXEL_FAST_TARGET_HEIGHT  256
#define VOXEL_SAMPLE_SHIFT        22
#define VOXEL_SAMPLE_ONE          4194304.0f
#define VOXEL_FAST_MAX_ADVANCE    20
#define VOXEL_STEP_BITS           5
#define VOXEL_STEP_MASK           31
#define VOXEL_PROJECTION_SHIFT    13
#define VOXEL_PROJECTION_ONE      8192.0f

typedef struct {
  BOOL valid;
  float draw_distance;
  float depth_delta;
  float height_scale;
  ULONG count;
  UBYTE maximum_step;
  ULONG sample[VOXEL_FAST_DEPTH_CAPACITY];
} voxel_fast_depth_lut_t;

static voxel_fast_depth_lut_t fast_depth_lut;
static UWORD fast_stop_index[VOXEL_FAST_TARGET_HEIGHT + 1];

static BOOL isPowerOfTwo(ULONG value)
{
  return value != 0 && (value & (value - 1)) == 0;
}

static LONG floorToLong(float value)
{
  LONG integer;

  integer = (LONG)value;
  if (value < (float)integer) {
    integer--;
  }
  return integer;
}

static LONG roundFloatToLong(float value)
{
  if (value >= 0.0f) {
    return (LONG)(value + 0.5f);
  }
  return (LONG)(value - 0.5f);
}

static ULONG floatToMapPhase(float value)
{
  LONG integer;
  LONG fraction;
  ULONG map_cell;

  integer = floorToLong(value);
  fraction = roundFloatToLong(
    (value - (float)integer) * VOXEL_SAMPLE_ONE
  );
  map_cell = (ULONG)integer & (VOXEL_FAST_MAP_SIZE - 1);
  if (fraction >= (LONG)VOXEL_SAMPLE_ONE) {
    fraction = 0;
    map_cell = (map_cell + 1) & (VOXEL_FAST_MAP_SIZE - 1);
  }

  return (map_cell << VOXEL_SAMPLE_SHIFT) + (ULONG)fraction;
}

static BOOL prepareFastDepthLut(const voxel_render_params_t *params)
{
  float depth_delta;
  int depth;
  ULONG count;

  if (fast_depth_lut.valid &&
      fast_depth_lut.draw_distance == params->draw_distance &&
      fast_depth_lut.depth_delta == params->depth_delta &&
      fast_depth_lut.height_scale == params->height_scale) {
    return TRUE;
  }

  depth_delta = 1.0f;
  depth = 1;
  count = 0;
  fast_depth_lut.maximum_step = 0;
  while ((float)depth < params->draw_distance) {
    float projection_scale;
    LONG projection_fixed;
    int next_depth;
    int depth_step;

    if (count >= VOXEL_FAST_DEPTH_CAPACITY) {
      fast_depth_lut.valid = FALSE;
      return FALSE;
    }

    projection_scale = params->height_scale / (float)depth;
    projection_fixed = roundFloatToLong(
      projection_scale * VOXEL_PROJECTION_ONE
    );

    depth_delta += params->depth_delta;
    next_depth = depth + (int)depth_delta;
    if (next_depth <= depth) {
      next_depth = depth + 1;
    }
    depth_step = next_depth - depth;
    if (depth_step > VOXEL_FAST_MAX_ADVANCE) {
      fast_depth_lut.valid = FALSE;
      return FALSE;
    }
    fast_depth_lut.sample[count] =
      ((ULONG)projection_fixed << VOXEL_STEP_BITS) | (ULONG)depth_step;
    if (depth_step > fast_depth_lut.maximum_step) {
      fast_depth_lut.maximum_step = (UBYTE)depth_step;
    }
    depth = next_depth;
    count++;
  }

  fast_depth_lut.draw_distance = params->draw_distance;
  fast_depth_lut.depth_delta = params->depth_delta;
  fast_depth_lut.height_scale = params->height_scale;
  fast_depth_lut.count = count;
  fast_depth_lut.valid = TRUE;
  return TRUE;
}

static BOOL prepareFastStopIndex(const voxel_render_params_t *params,
                                 int target_height, LONG camera_height,
                                 LONG camera_pitch_fixed)
{
  ULONG height;
  ULONG sample_index;
  LONG height_delta;

  if (target_height < 1 || target_height > VOXEL_FAST_TARGET_HEIGHT ||
      fast_depth_lut.count > 65535 ||
      params->height_map_max > VOXEL_HEIGHT_BOUND_DISABLED) {
    return FALSE;
  }

  if (params->height_map_max == VOXEL_HEIGHT_BOUND_DISABLED) {
    for (height = 0; height <= (ULONG)target_height; height++) {
      fast_stop_index[height] = (UWORD)fast_depth_lut.count;
    }
    return TRUE;
  }

  height_delta = camera_height - (LONG)params->height_map_max;
  if (height_delta < 0) {
    sample_index = 0;
    for (height = 0; height <= (ULONG)target_height; height++) {
      LONG threshold;

      threshold = (LONG)height << VOXEL_PROJECTION_SHIFT;
      while (sample_index < fast_depth_lut.count &&
             height_delta *
               (LONG)(fast_depth_lut.sample[sample_index] >>
                      VOXEL_STEP_BITS) +
               camera_pitch_fixed < threshold) {
        sample_index++;
      }
      fast_stop_index[height] = (UWORD)sample_index;
    }
  } else {
    LONG minimum_projection;

    minimum_projection = height_delta *
      (LONG)(fast_depth_lut.sample[fast_depth_lut.count - 1] >>
             VOXEL_STEP_BITS) +
      camera_pitch_fixed;
    for (height = 0; height <= (ULONG)target_height; height++) {
      LONG threshold;

      threshold = (LONG)height << VOXEL_PROJECTION_SHIFT;
      fast_stop_index[height] = minimum_projection >= threshold
        ? 0 : (UWORD)fast_depth_lut.count;
    }
  }
  return TRUE;
}

static void drawVerticalSpan(SAGE_Bitmap *bitmap, int x, int span_width,
                             int top, int bottom, UBYTE color,
                             voxel_render_stats_t *stats)
{
  UBYTE *destination;
  int y;

  if (top < 0) {
    top = 0;
  }
  if (bottom > (int)bitmap->height) {
    bottom = (int)bitmap->height;
  }
  if (top >= bottom || span_width <= 0) {
    return;
  }

  destination = (UBYTE *)bitmap->bitmap_buffer + top * bitmap->bpr + x;
  for (y = top; y < bottom; y++) {
    switch (span_width) {
      case 4:
        destination[3] = color;
      case 3:
        destination[2] = color;
      case 2:
        destination[1] = color;
      default:
        destination[0] = color;
        break;
    }
    destination += bitmap->bpr;
  }

  stats->spans++;
  stats->pixels += (ULONG)((bottom - top) * span_width);
}

#ifndef VOXEL_RENDERER_AMMX_TARGET
static void drawVerticalSpanFast(SAGE_Bitmap *bitmap, UBYTE *column_buffer,
                                 int x, int span_width,
                                 int top, int bottom, UBYTE color,
                                 voxel_render_stats_t *stats)
{
#ifndef VOXEL_RENDERER_AMMX_BUILD
  ULONG color32;
  UWORD color16;
#endif
  int rows;

  if (top < 0) {
    top = 0;
  }
  if (bottom > (int)bitmap->height) {
    bottom = (int)bitmap->height;
  }
  if (top >= bottom || span_width <= 0) {
    return;
  }

  rows = bottom - top;
#ifdef VOXEL_RENDERER_AMMX_BUILD
  {
    int span_column;

    for (span_column = 0; span_column < span_width; span_column++) {
      UBYTE *destination;

      destination = column_buffer +
        (ULONG)(x + span_column) * bitmap->height + top;
      memset(destination, color, (size_t)rows);
    }
  }
#else
  {
    UBYTE *destination;

    (void)column_buffer;
    destination = (UBYTE *)bitmap->bitmap_buffer + top * bitmap->bpr + x;
  color16 = (UWORD)(((UWORD)color << 8) | color);
  color32 = ((ULONG)color16 << 16) | color16;

  if (span_width == 4 && (bitmap->bpr & 3) == 0 &&
      ((size_t)destination & 3) == 0) {
    int count;

    count = rows;
    while (count-- > 0) {
      *((ULONG *)destination) = color32;
      destination += bitmap->bpr;
    }
  } else if (span_width == 3) {
    int count;

    count = rows;
    while (count-- > 0) {
      destination[0] = color;
      destination[1] = color;
      destination[2] = color;
      destination += bitmap->bpr;
    }
  } else if (span_width == 2 && (bitmap->bpr & 1) == 0 &&
             ((size_t)destination & 1) == 0) {
    int count;

    count = rows;
    while (count-- > 0) {
      *((UWORD *)destination) = color16;
      destination += bitmap->bpr;
    }
  } else if (span_width == 1) {
    int count;

    count = rows;
    while (count-- > 0) {
      destination[0] = color;
      destination += bitmap->bpr;
    }
  } else {
    int count;

    count = rows;
    while (count-- > 0) {
      destination[0] = color;
      if (span_width > 1) {
        destination[1] = color;
      }
      if (span_width > 2) {
        destination[2] = color;
      }
      if (span_width > 3) {
        destination[3] = color;
      }
      destination += bitmap->bpr;
    }
  }
  }
#endif

  stats->spans++;
  stats->pixels += (ULONG)(rows * span_width);
}
#endif

BOOL VoxelRenderC(const voxel_render_params_t *params,
                  voxel_render_stats_t *stats)
{
  SAGE_Bitmap *target;
  ULONG map_mask;
  float sin_angle;
  float cos_angle;
  float ray_x;
  float ray_y;
  float ray_step_x;
  float ray_step_y;
  int target_width;
  int target_height;
  int column;

  if (params == NULL || stats == NULL || params->target == NULL ||
      params->target->bitmap_buffer == NULL || params->height_map == NULL ||
      params->color_map == NULL || !isPowerOfTwo(params->map_size) ||
      params->horizontal_divisions < 1 || params->horizontal_divisions > 4 ||
      !(params->draw_distance > 1.0f) ||
      !(params->depth_delta >= 0.0f) ||
      !(params->height_scale >= 0.0f) || params->height_scale > 16000.0f ||
      params->target->depth != SBMP_DEPTH8 ||
      params->target->bpr < params->target->width) {
    return FALSE;
  }

  target = params->target;
  target_width = (int)target->width;
  target_height = (int)target->height;
  if (target_width <= 0 || target_height <= 0) {
    return FALSE;
  }
  memset(target->bitmap_buffer, params->clear_color,
         target->bpr * target->height);
  memset(stats, 0, sizeof(*stats));
  map_mask = params->map_size - 1;

  sin_angle = params->camera_sine;
  cos_angle = params->camera_cosine;
  ray_x = cos_angle + sin_angle;
  ray_y = sin_angle - cos_angle;
  ray_step_x = (-2.0f * sin_angle) / (float)target_width;
  ray_step_y = (2.0f * cos_angle) / (float)target_width;

  for (column = 0; column < target_width;
       column += params->horizontal_divisions) {
    float sample_x;
    float sample_y;
    float depth_delta;
    int depth;
    int visible_top;
    int column_offset;
    int span_width;

    depth_delta = 1.0f;
    depth = 1;
    visible_top = target_height;
    column_offset = (int)(params->base_vertical_offset +
                    params->camera_roll *
                    (column / (float)target_width - 0.5f) *
                    params->roll_scale);
    span_width = params->horizontal_divisions;
    if (column + span_width > target_width) {
      span_width = target_width - column;
    }
    stats->rays++;

    while (depth < params->draw_distance && visible_top > 0) {
      ULONG map_offset;
      int height_on_screen;

      sample_x = params->camera_x + ray_x * depth;
      sample_y = params->camera_y + ray_y * depth;
      map_offset = params->map_size * ((ULONG)floorToLong(sample_y) & map_mask);
      map_offset += (ULONG)floorToLong(sample_x) & map_mask;
      height_on_screen = (int)(
        (params->camera_height - params->height_map[map_offset]) /
        depth * params->height_scale + params->camera_pitch
      );
      if (height_on_screen < 0) {
        height_on_screen = 0;
      } else if (height_on_screen > target_height) {
        height_on_screen = target_height;
      }
      stats->depth_samples++;

      if (height_on_screen < visible_top) {
        drawVerticalSpan(
          target, column, span_width,
          height_on_screen + column_offset,
          visible_top + column_offset,
          params->color_map[map_offset], stats
        );
        visible_top = height_on_screen;
      }

      depth_delta += params->depth_delta;
      depth += (int)depth_delta;
    }

    ray_x += ray_step_x * params->horizontal_divisions;
    ray_y += ray_step_y * params->horizontal_divisions;
  }
  return TRUE;
}

BOOL VoxelRenderFastC(const voxel_render_params_t *params,
                      voxel_render_stats_t *stats)
{
  SAGE_Bitmap *target;
  voxel_render_stats_t local_stats;
  ULONG camera_x_phase;
  ULONG camera_y_phase;
  LONG camera_height;
  LONG camera_pitch_fixed;
  LONG advance_x[VOXEL_FAST_MAX_ADVANCE + 1];
  LONG advance_y[VOXEL_FAST_MAX_ADVANCE + 1];
  float ray_x;
  float ray_y;
  float ray_column_step_x;
  float ray_column_step_y;
  int target_width;
  int target_height;
  int column;
#ifdef VOXEL_RENDERER_AMMX_TARGET
  voxel_ammx_ray_t ammx_ray;
#endif

  if (params == NULL || stats == NULL || params->target == NULL ||
      params->target->bitmap_buffer == NULL || params->height_map == NULL ||
      params->color_map == NULL || params->map_size != VOXEL_FAST_MAP_SIZE ||
      params->horizontal_divisions < 1 || params->horizontal_divisions > 4 ||
      !(params->draw_distance > 1.0f) ||
      !(params->depth_delta >= 0.0f) ||
      !(params->height_scale >= 0.0f) || params->height_scale > 16000.0f ||
      params->target->depth != SBMP_DEPTH8 ||
      params->target->bpr < params->target->width) {
    return FALSE;
  }

  target = params->target;
  target_width = (int)target->width;
  target_height = (int)target->height;
  if (target_width <= 0 || target_height <= 0) {
    return FALSE;
  }
#ifdef VOXEL_RENDERER_AMMX_BUILD
  if (params->column_buffer == NULL ||
      (target_width & 7) != 0 || (target_height & 7) != 0) {
    return FALSE;
  }
#ifndef VOXEL_AMMX_HOST_TEST
  if ((target->bpr & 7) != 0 ||
      ((size_t)params->column_buffer & 7) != 0 ||
      ((size_t)target->bitmap_buffer & 7) != 0) {
    return FALSE;
  }
#endif
#endif

  ray_x = params->camera_cosine + params->camera_sine;
  ray_y = params->camera_sine - params->camera_cosine;
  if (!(ray_x >= -2.0f && ray_x <= 2.0f) ||
      !(ray_y >= -2.0f && ray_y <= 2.0f)) {
    return FALSE;
  }

  camera_height = roundFloatToLong(params->camera_height);
  camera_pitch_fixed = roundFloatToLong(
    params->camera_pitch * VOXEL_PROJECTION_ONE
  );
  if (!prepareFastDepthLut(params) ||
      !prepareFastStopIndex(params, target_height, camera_height,
                            camera_pitch_fixed)) {
    return FALSE;
  }

#ifdef VOXEL_RENDERER_AMMX_BUILD
  memset(params->column_buffer, params->clear_color,
         (size_t)target_width * target_height);
  if (target->bpr > target->width) {
    memset(target->bitmap_buffer, params->clear_color,
           target->bpr * target->height);
  }
#else
  memset(target->bitmap_buffer, params->clear_color,
         target->bpr * target->height);
#endif
  memset(&local_stats, 0, sizeof(local_stats));
  camera_x_phase = floatToMapPhase(params->camera_x);
  camera_y_phase = floatToMapPhase(params->camera_y);

#ifdef VOXEL_RENDERER_AMMX_TARGET
  ammx_ray.height_map = params->height_map;
  ammx_ray.color_map = params->color_map;
  ammx_ray.depth_lut = fast_depth_lut.sample;
  ammx_ray.stop_index = fast_stop_index;
  ammx_ray.camera_height = camera_height;
  ammx_ray.camera_pitch_fixed = camera_pitch_fixed;
  ammx_ray.target_height = (ULONG)target_height;
#endif

  ray_column_step_x = (-2.0f * params->camera_sine) /
                      (float)target_width * params->horizontal_divisions;
  ray_column_step_y = (2.0f * params->camera_cosine) /
                      (float)target_width * params->horizontal_divisions;
  for (column = 0; column < target_width;
       column += params->horizontal_divisions) {
    LONG ray_x_fixed;
    LONG ray_y_fixed;
    ULONG sample_x_phase;
    ULONG sample_y_phase;
#ifndef VOXEL_RENDERER_AMMX_TARGET
    ULONG sample_index;
    ULONG ray_sample_limit;
#endif
    int advance;
#ifndef VOXEL_RENDERER_AMMX_TARGET
    int visible_top;
#endif
    int column_offset;
    int span_width;

    ray_x_fixed = roundFloatToLong(ray_x * VOXEL_SAMPLE_ONE);
    ray_y_fixed = roundFloatToLong(ray_y * VOXEL_SAMPLE_ONE);
    advance_x[0] = 0;
    advance_y[0] = 0;
    for (advance = 1; advance <= fast_depth_lut.maximum_step; advance++) {
      advance_x[advance] = advance_x[advance - 1] + ray_x_fixed;
      advance_y[advance] = advance_y[advance - 1] + ray_y_fixed;
    }
    sample_x_phase = camera_x_phase + (ULONG)ray_x_fixed;
    sample_y_phase = camera_y_phase + (ULONG)ray_y_fixed;
#ifndef VOXEL_RENDERER_AMMX_TARGET
    visible_top = target_height;
    ray_sample_limit = fast_stop_index[visible_top];
#endif
    column_offset = (int)(params->base_vertical_offset +
                    params->camera_roll *
                    (column / (float)target_width - 0.5f) *
                    params->roll_scale);
    span_width = params->horizontal_divisions;
    if (column + span_width > target_width) {
      span_width = target_width - column;
    }
    local_stats.rays++;
#ifdef VOXEL_RENDERER_AMMX_TARGET
    ammx_ray.column_base = params->column_buffer +
      (ULONG)column * (ULONG)target_height;
    ammx_ray.advance_x = advance_x;
    ammx_ray.advance_y = advance_y;
    ammx_ray.sample_x_phase = sample_x_phase;
    ammx_ray.sample_y_phase = sample_y_phase;
    ammx_ray.column_offset = (LONG)column_offset;
    ammx_ray.span_width = (ULONG)span_width;
    VoxelRenderRayAMMX(&ammx_ray, &local_stats);
#else
    sample_index = 0;
    while (sample_index < ray_sample_limit && visible_top > 0) {
      ULONG map_offset;
      ULONG packed_sample;
      LONG projected_height;
      int height_on_screen;

      map_offset = ((sample_y_phase >> 12) & 0x000ffc00UL) |
                   (sample_x_phase >> VOXEL_SAMPLE_SHIFT);
      packed_sample = fast_depth_lut.sample[sample_index];
      advance = (int)(packed_sample & VOXEL_STEP_MASK);
      sample_x_phase += (ULONG)advance_x[advance];
      sample_y_phase += (ULONG)advance_y[advance];
      projected_height =
        (camera_height - params->height_map[map_offset]) *
        (LONG)(packed_sample >> VOXEL_STEP_BITS) +
        camera_pitch_fixed;
      if (projected_height <= 0) {
        height_on_screen = 0;
      } else {
        height_on_screen = (int)(projected_height >>
                                 VOXEL_PROJECTION_SHIFT);
        if (height_on_screen > target_height) {
          height_on_screen = target_height;
        }
      }
      if (height_on_screen < visible_top) {
        drawVerticalSpanFast(
          target,
#ifdef VOXEL_RENDERER_AMMX_BUILD
          params->column_buffer,
#else
          NULL,
#endif
          column, span_width,
          height_on_screen + column_offset,
          visible_top + column_offset,
          params->color_map[map_offset], &local_stats
        );
        visible_top = height_on_screen;
        ray_sample_limit = fast_stop_index[visible_top];
      }
      sample_index++;
    }

    local_stats.depth_samples += sample_index;
#endif
    ray_x += ray_column_step_x;
    ray_y += ray_column_step_y;
  }

#ifdef VOXEL_RENDERER_AMMX_BUILD
#ifdef VOXEL_AMMX_HOST_TEST
  transposeColumnBuffer(
    params->column_buffer, (UBYTE *)target->bitmap_buffer,
    target->width, target->height, target->bpr
  );
#else
  VoxelTransposeAMMX(
    params->column_buffer, (UBYTE *)target->bitmap_buffer,
    target->width, target->height, target->bpr
  );
#endif
#endif

  *stats = local_stats;
  return TRUE;
}
