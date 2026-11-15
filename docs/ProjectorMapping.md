# Projector mapping build

Based on ossia Score 3.9.0, with add-on revisions locked to the official 3.9.0
Windows release manifest. This fork extends the existing Window device's
Multi-Window Mapping interface for overlapping projector outputs.

## Calibration patterns

The Preview menu includes Global Text / Focus, Solid White, Solid 50% Gray,
Solid Red, Solid Green, and Solid Blue, alongside the original patterns.
The text pattern uses the shared input canvas and is cropped by each output's
source region, so lettering crosses the overlap consistently. Text repeats at
12, 18, 24, 36, 48, 72, and 96 pixels for focus and readability checks.

## Three projectors side by side

Set the input resolution to the full canvas, then position the three source
regions with the intended overlap. Assign their windows to the projector
displays in Desktop Layout. Select an output and expand Soft-Edge Blending.
“Calculate Blends from Overlap” sets edge widths for all outputs while retaining
their individual edge gamma values. The outside edges remain unblended.

For example, three 1920-pixel outputs with 240 pixels of overlap between each
pair use a 5280 × 1080 canvas. Their source rectangles are:

| Output | X | Y | Width | Height |
| --- | --- | --- | --- | --- |
| Left | 0 | 0 | 0.363636 | 1 |
| Center | 0.318182 | 0 | 0.363636 | 1 |
| Right | 0.636364 | 0 | 0.363636 | 1 |

The internal blend widths are 0.125 of each projector's output width. Refine
edge gamma against the white and gray patterns. Per-output red, green, and blue
gains match color and brightness; output gamma matches tonal response. Black
level raises the dark floor, fading out at blended edges, to help compensate
for real projector black leakage. These controls retain their neutral defaults
for existing projects. Physical projector matching still determines the final
calibration.

## Mesh warping

Select an output, set Mesh columns and Mesh rows, and click Create / Reset Mesh.
Each axis supports 2–17 points. Creation replaces that output's existing mesh
and initializes the new points from its four-corner perspective calibration.
Double-click the output to edit the lattice. Drag any point; Ctrl-drag gives
fine adjustment. Drags that invert or collapse a cell are rejected. Escape
leaves warp editing. Reset Warp restores a regular mesh of the same dimensions.
Use Four Corners removes the mesh and returns to the saved corner controls.

The GPU renders an actual triangle lattice. The calibration previews use the
same cell triangulation, rather than approximating the entire lattice with a
single four-corner transform.

## Edit a running NDI output

The NDI add-on uses a separately installed NDI runtime. Keep the runtime from
your existing working setup installed; it is not bundled with this Score
installer. On Windows, the add-on checks the NDI runtime environment variables
and then the normal DLL search path. This preserves the existing arrangement
used by upstream Score.

Start the existing score and NDI input normally. In the device explorer,
right-click the multi-window Window device and choose Edit Live Mapping….
The familiar mapping editor opens while the existing render node, input, and
output windows remain running. Changes to source regions, blends, mesh,
color calibration, and desktop placement apply as they are edited.

OK saves the changes through the normal undo command; Cancel restores the
settings from when the editor opened. Undo and redo of these mapping changes
also update the existing device in place. Output count, device name, mode,
input resolution, and graphics format remain fixed during this editor session
because changing those requires rebuilding the device. Use the normal device
settings while stopped for those changes. Calibration preview windows are
suppressed in the live editor so the actual NDI-fed picture remains visible.

## Existing scenes

Existing 3.9.0 mapping records default to four-corner warping and neutral output
calibration. Existing JSON keys and the original binary record layout are
retained when no new properties are used. New properties use optional JSON
fields or a marked binary extension. This provides loading of existing scenes
in this fork; opening newly mesh-calibrated files in an older Score build does
not reproduce the added calibration. Save a copy of a production scene for
initial verification on the projector computer.

## Windows USB package

The Projector mapping Windows installer workflow builds the application with
the upstream Windows SDK and NDI add-on, checks mapping serialization, and
packages the installer with this guide, the source commit, and a SHA-256 hash.
The separate Windows Qt checks exercise the mapping canvas and previews.
Copy the extracted artifact folder to a USB drive and run
Ossia-Score-Projector-Mapping-Windows-x64.exe on the Windows 11 computer.

An artifact exists only after the Windows workflow succeeds. Source changes,
local Qt checks, and shader compilation alone are not an installable build or
proof of live NDI behavior on the target GPU.

## Developer checks

```sh
cmake -S tests/projector-mapping -B build-mapping -G Ninja
cmake --build build-mapping
ctest --test-dir build-mapping --output-on-failure
python3 tests/projector-mapping/check_shader.py /path/to/qsb
```

The Qt-only checks can run independently of Score's third-party submodules.
They require Qt Widgets, Test, and Shader Tools on Windows and macOS. The pixel
checks compile the production shaders directly and verify mesh coverage, RGB
calibration, and preservation of negative and above-white HDR values. Windows
uses the D3D11 software renderer for these checks; macOS uses Metal.
Full mapping JSON/binary compatibility checks are in
`test_unit_output_mapping_serialization` when configured with
`-DSCORE_TESTING=ON`.
