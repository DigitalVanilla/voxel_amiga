#include <exec/types.h>

#include <sage/sage_error.h>

/*
 * SAGE's central init module references optional subsystems even when this
 * application does not request them. Keep the voxel build small by providing
 * unavailable-module stubs for audio, network and 3D.
 */
BOOL SAGE_InitAudioModule(VOID)
{
  SAGE_SetError(SERR_NOT_AVAILABLE);
  return FALSE;
}

BOOL SAGE_ReleaseAudioModule(VOID)
{
  return TRUE;
}

BOOL SAGE_InitNetworkModule(VOID)
{
  SAGE_SetError(SERR_NOT_AVAILABLE);
  return FALSE;
}

BOOL SAGE_ReleaseNetworkModule(VOID)
{
  return TRUE;
}

BOOL SAGE_Init3DModule(VOID)
{
  SAGE_SetError(SERR_NOT_AVAILABLE);
  return FALSE;
}

BOOL SAGE_Release3DModule(VOID)
{
  return TRUE;
}
