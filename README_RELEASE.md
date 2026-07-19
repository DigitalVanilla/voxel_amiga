# Voxel Amiga

A classic height-field voxel renderer for AmigaOS, built with SAGE and the
Amiga GCC toolchain. This branch is AMMX-only and requires an Apollo/Vampire
68080 system. The demo uses an 8-bit 320x200 screen and can cycle through
several AMMX-safe voxel layer sizes.

## Controls

- `W` / `S`: move forward and backward
- `A` / `D`: turn left and right
- Up / Down: raise and lower the camera
- `Q` / `E`: increase and decrease draw distance
- `Z` / `X`: decrease and increase depth detail
- `C` / `V`: increase and decrease terrain height scale
- `L`: cycle horizontal resolution from 1 to 4 pixels
- `P`: cycle layer size: `320x200`, `200x120`, `160x96`, `128x80`
- `F1` / `F2` / `F3`: select map banks `0-9`, `10-19`, and `20-29`
- `0`-`9`: load a map from the selected bank
- `M` / Shift+`M`: load the next or previous map
- `N`: toggle night palette
- `K`: toggle the debug/profiling overlay
- Escape or a mouse button: exit

## Debug

Every run creates `voxel_log.txt` beside the executable and mirrors it to
`RAM:voxel_log.txt`. Startup checkpoints,
renderer failures, shutdown progress, and SAGE log messages are written there
immediately so the final line remains useful after a crash.