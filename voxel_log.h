#ifndef VOXEL_LOG_H
#define VOXEL_LOG_H

#include <stdarg.h>
#include <exec/types.h>

BOOL VoxelLogOpen(const char *);
void VoxelLogClose(void);
void VoxelLog(const char *, ...);
void VoxelLogV(const char *, va_list);

#endif
