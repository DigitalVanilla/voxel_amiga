# Voxel Amiga

A classic height-field voxel renderer for AmigaOS, built with SAGE and the
Amiga GCC toolchain. The safe renderer targets a 68060 with an
68881-compatible FPU. A runtime-gated 68080/AMMX renderer is included for
Apollo/Vampire systems. The demo uses an 8-bit 320x200 screen with a 200x120
voxel layer.

## Requirements

- GNU Make
- Amiga GCC installed at `D:/SDK/amiga-gcc` (override `GCC_ROOT` if needed)
- An AmigaOS target with Picasso96 support

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

To compare the optimized renderer against the original C path on Windows:

```sh
make test-renderer
```

The comparison checks guarded frame buffers across all horizontal divisions,
camera wrapping and roll, padded row strides, and the minimum/maximum runtime
render controls. It requires the column-major 68080/AMMX algorithm to remain
byte-identical to Fast C, and also checks the exact global height bound enabled
and disabled across the fixed cases and 128 deterministic randomized camera
cases. The host test uses a C transpose because AMMX instructions cannot run
on Windows; the target build assembles the same 8x8 layout conversion with
AMMX `LOAD`, `VPERM`, and `STORE` instructions.

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
- `R`: cycle Fast C, 68080+AMMX, and reference C renderers (AMMX is skipped
  when no Apollo core is detected)
- `N`: toggle night palette
- `K`: toggle the debug/profiling overlay
- Escape or a mouse button: exit

The program currently loads `maps/map0.height.raw`, `maps/map0.color.raw`,
and `maps/map0.palette.raw`. These files preserve the indexed GIF data but
avoid decoding large images through Amiga DataTypes during startup. The source
GIFs remain in `maps/`; regenerate the runtime files on Windows with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/convert_maps.ps1
```

The profiling overlay reports the terrain renderer and screen-composition
times separately from presentation/VSync wait, along with ray, depth-sample,
span, and written-pixel counts. Fast C uses an inexpensive exact whole-map
height bound. The first 68080 path preserves that algorithm, writes vertical
spans contiguously into a 24,000-byte column buffer, and uses an exact AMMX
8x8 transpose into the SAGE bitmap. Its terrain traversal deliberately retains
the compatible 68060 instruction set because older Vampire cores can trap on
newer Apollo scalar Line-A opcodes. Only the transpose object is assembled for
AMMX, matching the strategy used by the bundled SAGE AMMX blitter. Fast C
remains the startup default and all AMMX execution stays dormant until selected.
As in the `spy_girl` demo, FPS is counted in
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
