#include <sage/sage_maths.h>

#include "voxel_math.h"

void VoxelFastSinCos(float radians, float *sine, float *cosine)
{
  LONG angle_index;

  angle_index = (LONG)(radians *
                ((float)SMTH_ANGLE_360 / VOXEL_FULL_TURN));
  while (angle_index < 0) {
    angle_index += SMTH_ANGLE_360;
  }
  while (angle_index >= SMTH_ANGLE_360) {
    angle_index -= SMTH_ANGLE_360;
  }

  *sine = SAGE_FastSine((WORD)angle_index);
  *cosine = SAGE_FastCosine((WORD)angle_index);
}
