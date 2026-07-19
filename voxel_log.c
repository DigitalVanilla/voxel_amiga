#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <dos/dos.h>
#include <proto/dos.h>

#include "voxel_log.h"

#define VOXEL_LOG_BUFFER_SIZE 1024
#define VOXEL_EMERGENCY_LOG "RAM:voxel_log.txt"

static BPTR voxel_log_file = 0;
static BPTR voxel_emergency_log_file = 0;

BOOL VoxelLogOpen(const char *filename)
{
  if (voxel_log_file != 0 || voxel_emergency_log_file != 0) {
    return TRUE;
  }

  voxel_log_file = Open((CONST_STRPTR)filename, MODE_NEWFILE);
  voxel_emergency_log_file = Open(
    (CONST_STRPTR)VOXEL_EMERGENCY_LOG, MODE_NEWFILE
  );
  return voxel_log_file != 0 || voxel_emergency_log_file != 0;
}

void VoxelLogClose(void)
{
  if (voxel_log_file != 0) {
    Flush(voxel_log_file);
    Close(voxel_log_file);
    voxel_log_file = 0;
  }
  if (voxel_emergency_log_file != 0) {
    Flush(voxel_emergency_log_file);
    Close(voxel_emergency_log_file);
    voxel_emergency_log_file = 0;
  }
}

void VoxelLogV(const char *format, va_list arguments)
{
  char buffer[VOXEL_LOG_BUFFER_SIZE];
  int written;
  LONG length;

  written = vsnprintf(buffer, sizeof(buffer), format, arguments);
  if (written < 0) {
    strcpy(buffer, "[log format error]\n");
  } else {
    buffer[sizeof(buffer) - 1] = 0;
  }
  length = (LONG)strlen(buffer);

  printf("%s", buffer);
  fflush(stdout);

  if (voxel_log_file != 0) {
    Write(voxel_log_file, buffer, length);
    Flush(voxel_log_file);
  }
  if (voxel_emergency_log_file != 0) {
    Write(voxel_emergency_log_file, buffer, length);
    Flush(voxel_emergency_log_file);
  }
}

void VoxelLog(const char *format, ...)
{
  va_list arguments;

  va_start(arguments, format);
  VoxelLogV(format, arguments);
  va_end(arguments);
}
