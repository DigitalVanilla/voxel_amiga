#include <string.h>
#include <stddef.h>
#include <sage/sage_compiler.h>

#include "voxel_renderer.h"

#define VOXEL_DEPTH_CAPACITY    2048
#define VOXEL_MAP_SIZE          1024
#define VOXEL_TARGET_HEIGHT_MAX 256
#define VOXEL_SAMPLE_SHIFT      22
#define VOXEL_SAMPLE_ONE        4194304.0f
#define VOXEL_MAX_ADVANCE       20
#define VOXEL_STEP_BITS         5
#define VOXEL_STEP_MASK         31
#define VOXEL_PROJECTION_SHIFT  13
#define VOXEL_PROJECTION_ONE    8192.0f

typedef struct {
  BOOL valid;
  float draw_distance;
  float depth_delta;
  float height_scale;
  ULONG count;
  UBYTE maximum_step;
  ULONG sample[VOXEL_DEPTH_CAPACITY];
} voxel_depth_lut_t;

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

extern VOID ASM VoxelTransposeAMMX(
  REG(a0, const UBYTE *source),
  REG(a1, UBYTE *destination),
  REG(d0, UWORD width),
  REG(d1, UWORD height),
  REG(d2, ULONG destination_bpr)
);

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

static voxel_depth_lut_t depth_lut;
static UWORD stop_index[VOXEL_TARGET_HEIGHT_MAX + 1];

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
  map_cell = (ULONG)integer & (VOXEL_MAP_SIZE - 1);
  if (fraction >= (LONG)VOXEL_SAMPLE_ONE) {
    fraction = 0;
    map_cell = (map_cell + 1) & (VOXEL_MAP_SIZE - 1);
  }

  return (map_cell << VOXEL_SAMPLE_SHIFT) + (ULONG)fraction;
}

static BOOL prepareDepthLut(const voxel_render_params_t *params)
{
  float depth_delta;
  int depth;
  ULONG count;

  if (depth_lut.valid &&
      depth_lut.draw_distance == params->draw_distance &&
      depth_lut.depth_delta == params->depth_delta &&
      depth_lut.height_scale == params->height_scale) {
    return TRUE;
  }

  depth_delta = 1.0f;
  depth = 1;
  count = 0;
  depth_lut.maximum_step = 0;
  while ((float)depth < params->draw_distance) {
    float projection_scale;
    LONG projection_fixed;
    int next_depth;
    int depth_step;

    if (count >= VOXEL_DEPTH_CAPACITY) {
      depth_lut.valid = FALSE;
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
    if (depth_step > VOXEL_MAX_ADVANCE) {
      depth_lut.valid = FALSE;
      return FALSE;
    }
    depth_lut.sample[count] =
      ((ULONG)projection_fixed << VOXEL_STEP_BITS) | (ULONG)depth_step;
    if (depth_step > depth_lut.maximum_step) {
      depth_lut.maximum_step = (UBYTE)depth_step;
    }
    depth = next_depth;
    count++;
  }

  depth_lut.draw_distance = params->draw_distance;
  depth_lut.depth_delta = params->depth_delta;
  depth_lut.height_scale = params->height_scale;
  depth_lut.count = count;
  depth_lut.valid = TRUE;
  return TRUE;
}

static BOOL prepareStopIndex(const voxel_render_params_t *params,
                             int target_height, LONG camera_height,
                             LONG camera_pitch_fixed)
{
  ULONG height;
  ULONG sample_index;
  LONG height_delta;

  if (target_height < 1 || target_height > VOXEL_TARGET_HEIGHT_MAX ||
      depth_lut.count > 65535 ||
      params->height_map_max > VOXEL_HEIGHT_BOUND_DISABLED) {
    return FALSE;
  }

  if (params->height_map_max == VOXEL_HEIGHT_BOUND_DISABLED) {
    for (height = 0; height <= (ULONG)target_height; height++) {
      stop_index[height] = (UWORD)depth_lut.count;
    }
    return TRUE;
  }

  height_delta = camera_height - (LONG)params->height_map_max;
  if (height_delta < 0) {
    sample_index = 0;
    for (height = 0; height <= (ULONG)target_height; height++) {
      LONG threshold;

      threshold = (LONG)height << VOXEL_PROJECTION_SHIFT;
      while (sample_index < depth_lut.count &&
             height_delta *
               (LONG)(depth_lut.sample[sample_index] >>
                      VOXEL_STEP_BITS) +
               camera_pitch_fixed < threshold) {
        sample_index++;
      }
      stop_index[height] = (UWORD)sample_index;
    }
  } else {
    LONG minimum_projection;

    minimum_projection = height_delta *
      (LONG)(depth_lut.sample[depth_lut.count - 1] >>
             VOXEL_STEP_BITS) +
      camera_pitch_fixed;
    for (height = 0; height <= (ULONG)target_height; height++) {
      LONG threshold;

      threshold = (LONG)height << VOXEL_PROJECTION_SHIFT;
      stop_index[height] = minimum_projection >= threshold
        ? 0 : (UWORD)depth_lut.count;
    }
  }
  return TRUE;
}

BOOL VoxelRenderAMMX(const voxel_render_params_t *params,
                     voxel_render_stats_t *stats)
{
  SAGE_Bitmap *target;
  voxel_render_stats_t local_stats;
  voxel_ammx_ray_t ammx_ray;
  ULONG camera_x_phase;
  ULONG camera_y_phase;
  LONG camera_height;
  LONG camera_pitch_fixed;
  LONG advance_x[VOXEL_MAX_ADVANCE + 1];
  LONG advance_y[VOXEL_MAX_ADVANCE + 1];
  float ray_x;
  float ray_y;
  float ray_column_step_x;
  float ray_column_step_y;
  int target_width;
  int target_height;
  int column;

  if (params == NULL || stats == NULL || params->target == NULL ||
      params->target->bitmap_buffer == NULL || params->height_map == NULL ||
      params->color_map == NULL || params->column_buffer == NULL ||
      params->map_size != VOXEL_MAP_SIZE ||
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
  if (target_width <= 0 || target_height <= 0 ||
      (target_width & 7) != 0 || (target_height & 7) != 0 ||
      (target->bpr & 7) != 0 ||
      ((size_t)params->column_buffer & 7) != 0 ||
      ((size_t)target->bitmap_buffer & 7) != 0) {
    return FALSE;
  }

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
  if (!prepareDepthLut(params) ||
      !prepareStopIndex(params, target_height, camera_height,
                        camera_pitch_fixed)) {
    return FALSE;
  }

  memset(params->column_buffer, params->clear_color,
         (size_t)target_width * target_height);
  if (target->bpr > target->width) {
    memset(target->bitmap_buffer, params->clear_color,
           target->bpr * target->height);
  }
  memset(&local_stats, 0, sizeof(local_stats));
  camera_x_phase = floatToMapPhase(params->camera_x);
  camera_y_phase = floatToMapPhase(params->camera_y);

  ammx_ray.height_map = params->height_map;
  ammx_ray.color_map = params->color_map;
  ammx_ray.depth_lut = depth_lut.sample;
  ammx_ray.stop_index = stop_index;
  ammx_ray.camera_height = camera_height;
  ammx_ray.camera_pitch_fixed = camera_pitch_fixed;
  ammx_ray.target_height = (ULONG)target_height;

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
    int advance;
    int column_offset;
    int span_width;

    ray_x_fixed = roundFloatToLong(ray_x * VOXEL_SAMPLE_ONE);
    ray_y_fixed = roundFloatToLong(ray_y * VOXEL_SAMPLE_ONE);
    advance_x[0] = 0;
    advance_y[0] = 0;
    for (advance = 1; advance <= depth_lut.maximum_step; advance++) {
      advance_x[advance] = advance_x[advance - 1] + ray_x_fixed;
      advance_y[advance] = advance_y[advance - 1] + ray_y_fixed;
    }
    sample_x_phase = camera_x_phase + (ULONG)ray_x_fixed;
    sample_y_phase = camera_y_phase + (ULONG)ray_y_fixed;
    column_offset = (int)(params->base_vertical_offset +
                    params->camera_roll *
                    (column / (float)target_width - 0.5f) *
                    params->roll_scale);
    span_width = params->horizontal_divisions;
    if (column + span_width > target_width) {
      span_width = target_width - column;
    }

    local_stats.rays++;
    ammx_ray.column_base = params->column_buffer +
      (ULONG)column * (ULONG)target_height;
    ammx_ray.advance_x = advance_x;
    ammx_ray.advance_y = advance_y;
    ammx_ray.sample_x_phase = sample_x_phase;
    ammx_ray.sample_y_phase = sample_y_phase;
    ammx_ray.column_offset = (LONG)column_offset;
    ammx_ray.span_width = (ULONG)span_width;
    VoxelRenderRayAMMX(&ammx_ray, &local_stats);

    ray_x += ray_column_step_x;
    ray_y += ray_column_step_y;
  }

  VoxelTransposeAMMX(
    params->column_buffer, (UBYTE *)target->bitmap_buffer,
    target->width, target->height, target->bpr
  );

  *stats = local_stats;
  return TRUE;
}
