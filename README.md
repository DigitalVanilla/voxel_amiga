# Voxel Amiga

A classic height-field voxel renderer for AmigaOS, built with SAGE and the
Amiga GCC toolchain. This branch is AMMX-only and requires an Apollo/Vampire
68080 system. The demo uses an 8-bit 320x200 screen and can cycle through
several AMMX-safe voxel layer sizes.

## Requirements

- GNU Make
- Amiga GCC installed at `D:/SDK/amiga-gcc` (override `GCC_ROOT` if needed)
- An Apollo/Vampire 68080 AmigaOS target with Picasso96 support

SAGE sources and the legacy support headers required by this project are kept
under `include/`, so no separate SAGE installation is needed.

## Build

```sh
make
```

For a clean build:

```sh
make rebuild
```

The executable is written to `build/bin/voxel_amiga`. A runnable directory,
including all map assets, is written to `build/package/voxel_amiga`.

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

The program loads `PROGDIR:maps/map0.*.raw` through `PROGDIR:maps/map29.*.raw`, so the runtime folder can be moved or launched from any current drawer. These files
preserve the indexed GIF data but avoid decoding large images through Amiga
DataTypes during startup. The source GIFs remain in `maps/`; regenerate the
runtime files on Windows with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/convert_maps.ps1
```

The profiling overlay reports the terrain renderer and screen-composition
times separately from presentation/VSync wait, along with ray, depth-sample,
span, and written-pixel counts. The renderer uses an inexpensive exact
whole-map height bound, writes vertical spans contiguously into an aligned
column buffer, calls a small hand-written ray/span routine, and uses an exact
AMMX 8x8 transpose into the SAGE bitmap. The AMMX assembly avoids newer Apollo
scalar Line-A opcodes; AMMX is used only where the renderer touches contiguous
memory blocks. As in the `spy_girl` demo, FPS is counted in
the main loop with its own SAGE timer; it does not activate SAGE's asynchronous
frame-counter interrupt. Profiling uses a second dedicated timer.

The bundled SAGE FPS callback is GCC-safe: it is a nested timer callback that
returns with `RTS`, while the timer dispatcher preserves its loop registers.
Declaring that callback as a CPU interrupt would make GCC emit `RTE` and
corrupt the timer interrupt stack.

Every run creates `voxel_log.txt` beside the executable and mirrors it to
`RAM:voxel_log.txt`. Startup checkpoints,
renderer failures, shutdown progress, and SAGE log messages are written there
immediately so the final line remains useful after a crash.
