# VkGigaTracer

A portable desktop **offline GPU path tracer** for **Windows and Linux** that
renders 3D scenes into 2D images at resolutions up to **80K (76,800 × 43,200 ≈
3.32 gigapixels)** on ordinary consumer hardware — including 4 GB GPUs and
Vulkan-capable iGPUs — **without ever failing for memory or size reasons.**

All GPU work is done through **Vulkan only** (via dynamic loading with volk).
Each platform gets a **single self-contained executable** (`VkGigaTracer.exe` on
Windows, `VkGigaTracer` on Linux) that runs on a clean machine whose only
relevant software is a GPU driver.

How the Linux port was done (analysis, decisions, step-by-step plan) is
recorded in [PORTING.md](PORTING.md).

---

## Download

Every push to `main` is built and tested by GitHub Actions on both platforms.
The packages are attached to each run of the
[build workflow](../../actions/workflows/build.yml) as artifacts:

- `VkGigaTracer-<version>-windows-x64.zip`
- `VkGigaTracer-<version>-linux-x86_64.tar.gz`

Each contains the executable, `fonts/`, this README and the license in one
folder. Extract it anywhere and run the executable from there; nothing is
installed.

---

## System requirements (to run)

- **Windows:** Windows 10/11, x64.
- **Linux:** x86_64 with glibc 2.34 or newer: **Ubuntu 22.04+**, Debian 12+,
  Fedora 35+, RHEL 9 and derivatives. Runs in an X11 session, or on Wayland
  desktops through XWayland (enabled by default on GNOME and KDE).
- Any **Vulkan 1.1** capable GPU + driver (roughly 2016 or newer). No Vulkan SDK
  or runtime is required.
  - Windows drivers ship `vulkan-1.dll`.
  - On Linux the loader is `libvulkan.so.1` (`libvulkan1` on Ubuntu/Debian),
    plus the GPU's Vulkan driver: Mesa (`mesa-vulkan-drivers`) for AMD and
    Intel, or NVIDIA's proprietary driver. Both are installed by default on
    most desktop distributions.
- **Linux file dialogs** use `zenity` (GNOME) or `kdialog` (KDE), which desktop
  distributions normally include. Without either, the app asks you to type the
  path instead.
- **Hardware ray tracing is used automatically** when the GPU supports
  `VK_KHR_ray_query` (NVIDIA RTX, AMD RX 6000+, Intel Arc). Otherwise a
  compute-only BVH path tracer is used, so ~2016-era GPUs and iGPUs still work.
- **Scratch disk space.** The full-resolution image is stored in a temporary
  file on disk (see architecture below). At 80K this file is **≈ 12.4 GiB**, so
  you need roughly **13 GB free** on the scratch drive. Smaller resolutions need
  proportionally less (4K ≈ 32 MB, 8K ≈ 127 MB, 40K ≈ 3.1 GiB). This is the only
  resource the app ever asks you about, and only before a render starts.
  By default the scratch file goes in a **`temp` folder next to the exe**
  (created on first use), so it's the exe's drive that needs the space. If that
  folder can't be created or written (e.g. a read-only install location), the
  system temp folder is used instead. You can pick any other folder under
  **Performance → Choose scratch folder…**.

The Windows exe imports only standard Windows system DLLs (`KERNEL32`,
`USER32`, `GDI32`, `IMM32`, `OLE32`, `SHELL32`). The Linux binary links only
`libc` and `libm`; the C++ runtime is linked in statically, and the X11
libraries are loaded at runtime by GLFW. On both, Vulkan is loaded dynamically
at runtime and all SPIR-V shaders are embedded in the binary. The only
companion files are the optional UI fonts in `fonts/` (see below).

---

## Building on Windows

- **Visual Studio 2022 or newer** (verified with the VS 2026 / v18 build tools).
- **CMake ≥ 3.24.**
- **Internet access** for the first configure (dependencies are fetched).

Nothing else is needed. In particular you do **not** need the Vulkan SDK, NASM,
or a system Python: a minimal embeddable Python is downloaded automatically and
used only to run glslang's build-time code generators.

### Steps

Run each command from the repository root.

1. Configure:
   ```bat
   cmake -B build -G "Visual Studio 18 2026" -A x64
   ```
   Success: the last line is `-- Build files have been written to: …/build`.
   (Use `-G "Visual Studio 17 2022"` on a VS 2022 machine.)
2. Build:
   ```bat
   cmake --build build --config Release
   ```
   Success: `build/Release/VkGigaTracer.exe` exists, with a `fonts` folder next to it.
3. Run the tests (optional, see below):
   ```bat
   ctest --test-dir build -C Release --output-on-failure
   ```
   Success: `100% tests passed, 0 tests failed out of 45`.
4. Make the release zip (optional):
   ```bat
   cd build && cpack -C Release
   ```
   Success: `build/VkGigaTracer-1.0.0-windows-x64.zip` is created.

The first configure downloads and builds every dependency, so it takes a few
minutes; later builds are incremental.

## Building on Linux

Verified on Ubuntu 22.04 with GCC 11.4 and on Ubuntu 26.04 with GCC 15. Build
on the oldest distribution you want to support: the binary runs on systems whose
glibc is at least as new as the build machine's.

Run each command in a terminal, from the repository root.

1. Install the build tools and X11 headers:
   ```sh
   sudo apt install build-essential ninja-build pkg-config python3-pip xorg-dev libxkbcommon-dev
   ```
   Success: the command ends without `E:` errors.
2. Install a current CMake (Ubuntu 22.04's packaged CMake is 3.22; the project needs 3.24+; skip this on newer distributions):
   ```sh
   python3 -m pip install --user cmake
   ```
   Success: `cmake --version` prints 3.24 or newer (log out and back in if `cmake` is not found yet).
3. Configure:
   ```sh
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   ```
   Success: the last line is `-- Build files have been written to: …/build`.
4. Build:
   ```sh
   cmake --build build
   ```
   Success: `build/VkGigaTracer` exists, with a `fonts` folder next to it.
5. Run the unit tests (optional):
   ```sh
   ctest --test-dir build -LE gpu --output-on-failure
   ```
   Success: `100% tests passed out of 43`.
6. Run the GPU tests (optional; needs a Vulkan driver, and `xvfb` if there is no display):
   ```sh
   xvfb-run -a ctest --test-dir build -L gpu --output-on-failure
   ```
   Success: `100% tests passed out of 2`.
7. Make the release tarball (optional):
   ```sh
   cd build && cpack
   ```
   Success: `build/VkGigaTracer-1.0.0-linux-x86_64.tar.gz` is created.

On Linux the system `python3` runs glslang's code generators, and GLFW is built
for X11 only.

## Dependencies

All third-party libraries are pulled with `FetchContent` at pinned release tags
(or a pinned commit) and built from source, statically linked:

| Dependency | Tag | Role |
|---|---|---|
| Vulkan-Headers | v1.3.296 | Vulkan API headers |
| volk | vulkan-sdk-1.3.296.0 | dynamic Vulkan loader (no `vulkan-1.lib`) |
| VulkanMemoryAllocator | v3.1.0 | all GPU allocations |
| GLFW | 3.4 | window + surface |
| Dear ImGui | v1.91.5 | UI (GLFW + Vulkan backends) |
| glslang | 14.3.0 | GLSL → SPIR-V at build time (host tool) |
| tinyobjloader | v1.0.6 | OBJ/MTL import |
| zlib | v1.3.1 | PNG compression |
| libpng | v1.6.44 | streaming PNG encode |
| libjpeg-turbo | 3.0.4 | streaming JPEG encode (SIMD off, no NASM) |
| portable-file-dialogs | commit c12ea8c | native file dialogs + message boxes (Linux only) |
| doctest | v2.4.11 | unit tests only (not linked into the app) |

The two SPIR-V variants of the path tracer are compiled at build time by
glslang and embedded into the executable as `constexpr` byte arrays
(`cmake/embed_spirv.cmake`), so no shader files are needed at runtime. The UI
fonts (Roboto) are copied into `fonts/` next to the exe after every build; if
they are missing the app falls back to a system font (Segoe UI on Windows;
DejaVu, Liberation or Noto Sans on Linux), then ImGui's built-in font.

### Running the tests

The build also produces a test suite, run through CTest (see the build steps):

- **`VkGigaTracerTests`**: CPU-only unit tests (doctest) for the render option
  tables, the disk-backed image and scratch-folder selection, the PNG / JPEG /
  PPM writers, OBJ/MTL import (including non-ASCII paths), the demo scene, the
  BVH (checked against brute-force ray intersection), the GPU struct layouts,
  and the platform layer.
- **`selftest_render` + `selftest_golden`** (labelled `gpu`): run
  `VkGigaTracer.exe --selftest`, then compare its output with the reference
  image `tests/golden/selftest_1080p_16spp.png` using `ppm_compare`. The
  comparison uses PSNR over 16×16 block averages, so normal path-tracing noise
  and small GPU-to-GPU differences pass, while real changes in brightness,
  color or geometry fail.

On a machine without a Vulkan GPU, skip the GPU tests with `-LE gpu`, or on
Linux run them on Mesa's CPU Vulkan driver (lavapipe, part of
`mesa-vulkan-drivers`) under `xvfb-run`, which is what CI does. Building with
`-DVKGT_BUILD_TESTS=OFF` leaves the test targets out entirely.

CI (`.github/workflows/build.yml`) runs on every push: the Linux job builds in
an Ubuntu 22.04 container and runs all tests including the self-test; the
Windows job builds without a Vulkan SDK and runs the unit tests. Both publish
their package.

---

## Using the app

Two tabs:

1. **Setup & Preview** — pick resolution, samples-per-pixel, max bounces,
   exposure, firefly clamp, sky, and backend; load the built-in **Demo Scene**
   or an **OBJ** file; and watch a live, interactive, progressively-accumulated
   path-traced preview (left-drag orbits, right-drag pans, the wheel dollies; the
   preview resolution is capped so it stays interactive on weak GPUs). Press
   **Render** to start the full out-of-core render.

2. **Render Result** — shows tiles filling in live, with tiles done / total,
   current tile spp, elapsed time, ETA, MRays/s, and a **Cancel** button. The
   viewer always letterbox-fits the whole image to the window, so an 80K render
   is fully visible on a 1080p monitor; the mouse wheel zooms and dragging pans.
   Zoom past the preview's pixel density and the visible region is streamed
   straight out of the memory-mapped file at full resolution. **Save Image…**
   writes PNG, JPEG, or PPM.

### Render options

All resolutions are 16:9, selected from a dropdown:

| Label | Pixels | | Label | Pixels |
|---|---|---|---|---|
| 480p  | 854 × 480     | | 10K | 9,600 × 5,400 |
| 720p  | 1,280 × 720   | | 15K | 14,400 × 8,100 |
| 1080p | 1,920 × 1,080 | | 20K | 19,200 × 10,800 |
| 2K    | 2,560 × 1,440 | | 40K | 38,400 × 21,600 |
| 4K    | 3,840 × 2,160 | | 60K | 57,600 × 32,400 |
| 6K    | 5,760 × 3,240 | | 80K | 76,800 × 43,200 |
| 8K    | 7,680 × 4,320 | |     |               |

- **Samples per pixel:** 1, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
  (1/4/8 are handy for fast look-dev; the default is 16).
- **Max bounces:** 16, 32, 48, 64.
- **Tile size** (Performance): 128 / 256 / 512 (default 256).
- Plus exposure, firefly clamp, sky on/off + intensity, backend override
  (Auto / Hardware RT / Compute), and a scratch-folder picker (Performance; shows
  the default `temp` folder next to the exe until you choose another).

### Saving

- **PNG** — lossless, available at every resolution (streamed via libpng
  `png_write_row`).
- **JPEG** — streamed via libjpeg-turbo `jpeg_write_scanlines`, quality 1–100.
  JPEG is physically limited to 65,500 px per side, so the option is greyed out
  (with a tooltip) whenever width or height exceeds that — i.e. 80K offers
  PNG/PPM only, while 60K and below allow JPEG.
- **PPM (P6 binary)** — raw RGB pixels behind a tiny ASCII header, written almost
  directly from the mapped file. **GIMP opens P6 PPM natively** with no import
  dialog, which makes it the easiest way to inspect a true-pixel gigapixel result.

Saving runs on a worker thread with its own progress bar and cancel, and streams
row chunks straight from the mapped file (RGBA→RGB on the fly), so peak extra RAM
stays small regardless of resolution.

---

## Architecture: how resolution is decoupled from VRAM and RAM

The headline trick is that **no full-resolution buffer is ever allocated** — not
in VRAM, not in system RAM.

1. **Disk-backed image.** At render start the app creates a scratch file of
   `width × height × 4` bytes (RGBA8) in the scratch folder (by default `temp/`
   next to the exe) and maps it with a single 64-bit mapping (`MapViewOfFile`
   on Windows, `mmap` on Linux). The renderer writes finished pixels straight
   into this mapping. The image's size is therefore bounded only by free disk
   space; 80K is ≈ 12.4 GiB on disk and a few hundred MB of committed host RAM.
   On Linux the file's blocks are reserved up front with `posix_fallocate`, so
   a disk that fills up mid-render can't crash the app through the mapping.
   (`src/mmap_image.{h,cpp}` for folder selection and free-space checks,
   `src/mmap_image_win32.cpp` / `src/mmap_image_posix.cpp` for the mapping)

2. **Tiles.** The image is split into tiles (default 256×256). The GPU holds a
   **single** RGBA32F accumulation tile (≈ 1–4 MiB) plus small staging — GPU
   memory use is **constant with respect to output resolution**. 80K is 50,700
   tiles; each is path-traced, resolved (÷spp → exposure → ACES → sRGB → RGBA8),
   read back, and written into the mapped file, then box-downsampled into a small
   preview. (`src/renderer.{h,cpp}`)

3. **GPU watchdog safety.** Windows resets the GPU after ~2 s of unyielding
   work (TDR), and Linux drivers have similar hang watchdogs. Every submit
   targets ≈100 ms: the spp-per-batch is auto-tuned from measured submit
   times, and if even 1 spp on a tile is too slow (huge bounce counts on a weak
   iGPU) the tile is subdivided down to 128² or 64². One submit → one fence wait
   → yield; work is never chained unbounded.

4. **Threading.** The UI thread (GLFW/ImGui/present) stays responsive while a
   dedicated worker thread records, submits, and fence-waits the render. Queue
   submission is guarded by a mutex only around the brief submit call, so the UI
   keeps presenting. A dedicated compute queue family is used when available.

5. **Cancel & recovery.** Cancel takes effect between batches (< 1 s) and the
   partial image stays viewable and savable. On `VK_ERROR_DEVICE_LOST` the engine
   halves the batch (then the tile), recreates the device and all resources, and
   resumes from the next unfinished tile — every completed tile already lives in
   the mapped file. Every `VkResult` is routed through one checked handler that
   logs to `gigatracer.log` next to the exe; the app never calls `abort()`/`exit()`
   on a Vulkan error.

6. **Two-level viewer.** No GPU texture can hold a gigapixel image
   (`maxImageDimension2D` is typically 16384), so the viewer uses a downsampled
   **preview texture** (long side ≤ 4096) for fitted/overview zooms and streams
   the visible full-resolution region out of the mapped file into a screen-sized
   **detail texture** when the zoom exceeds the preview's density. 100% zoom shows
   true rendered pixels. (`src/viewer.{h,cpp}`)

### Path tracer

One GLSL compute kernel (`shaders/pathtrace.comp` + `shaders/common.glsl`,
`local_size 8×8`) is compiled into two SPIR-V variants via `#define USE_RAY_QUERY`:

- **Hardware RT:** Vulkan 1.2 + `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`;
  a BLAS/TLAS is built over all triangles and intersected with `rayQueryEXT` inside
  the compute shader (`src/accel.{h,cpp}`).
- **Compute fallback:** a CPU-built binned-SAH BVH (`src/bvh.{h,cpp}`) uploaded as
  SSBOs, traversed iteratively with a fixed 64-entry stack and Möller–Trumbore.

Both consume identical triangle data, so the primitive index reported by the
hardware AS indexes the same triangle buffer the fallback uses. If acceleration
structure creation fails for any reason, the engine **silently falls back to the
compute path**.

The integrator is unidirectional path tracing with cosine-weighted diffuse
sampling, **next-event estimation on emissive triangles with MIS (power
heuristic)**, GGX metal, dielectric glass, PCG RNG seeded per (pixel, sample),
Russian roulette after bounce 8 (never past the user's bounce cap), a per-sample
firefly clamp, a toggleable gradient sky, and a fitted-ACES + sRGB tonemap.
Materials are mapped deterministically from MTL: Lambert (`Kd`), GGX metal
(`Ks`, roughness from `Ns`), glass (`Ni > 1` / `d < 1`), emissive (`Ke > 0`).
If a loaded OBJ has no emissive material and the sky is off, the sky is
auto-enabled so renders are never black.

---

## Verification

Running `VkGigaTracer --selftest` performs a headless (hidden-window)
render of the demo scene at 1080p / 16 spp / 16 bounces, writes
`selftest_out.ppm` to the **current working directory**, and logs progress to
`gigatracer.log` next to the exe. This is how the render path is validated
without opening the UI; the `selftest_golden` test (see
[Running the tests](#running-the-tests)) checks the image against a reference.

This repository was built and run on an **Intel UHD Graphics** iGPU (the
compute fallback backend), where the self-test renders the Cornell demo to a
correct, physically-plausible image (red/green walls, color bleeding, sky) in
~12 s and saves a 5.9 MB PPM. Two runs on the same GPU produce bit-identical
images. The Debug build (which enables the Khronos validation layer when it is
installed) runs both the self-test and the interactive UI, including window
resizes, with **zero validation messages**.

On Linux the same self-test passes on Mesa's CPU Vulkan driver (lavapipe):
on Ubuntu 22.04 through the compute backend (≈ 50 s), and on Ubuntu 26.04,
whose lavapipe supports `VK_KHR_ray_query`, through the **hardware ray-query
backend**, both matching the Windows reference image (mean absolute error
≤ 0.001 per channel). That is also how the ray-query path, which the Intel test
iGPU can't run, was verified. The extracted release packages pass the
self-test on both platforms.

## Design decisions / notes

- **VS 2026 toolchain.** This repo was built and verified with the Visual Studio
  v18 (2026) build tools; the generator string above reflects that. VS 2022
  (`Visual Studio 17 2022`) works the same way.
- **Embeddable Python.** glslang's build needs a Python interpreter for its code
  generators. To keep the build machine requirement at "VS + CMake + internet",
  CMake fetches the official Windows embeddable Python automatically and points
  glslang at it; nothing is installed system-wide. (On Linux the system
  `python3` is used instead.)
- **Platform layer.** Everything OS-specific (DPI awareness, COM, native file
  dialogs, message boxes, the exe's own folder, the memory-mapped scratch file)
  lives behind `src/platform.h` and `src/mmap_image.h`, implemented in
  `src/platform_win32.cpp` / `src/mmap_image_win32.cpp` (Windows) and
  `src/platform_posix.cpp` / `src/mmap_image_posix.cpp` (Linux); CMake picks
  the right pair. The rest of the code is plain C++20 using
  `std::filesystem::path` for every file path, so non-ASCII folder and file
  names work throughout.
- **Linux dialogs.** portable-file-dialogs drives `zenity`/`kdialog`, so no GTK
  or Qt is linked into the app. Windows keeps its native `IFileDialog`.
- **OBJs from Windows.** `mtllib` references with backslashes or the wrong
  letter case (`Materials\Scene.MTL`) still resolve on Linux's case-sensitive
  file systems.
- **X11 only for now.** GLFW is built without Wayland support; Wayland desktops
  run the app through XWayland.
- **Line endings.** `.gitattributes` keeps every text file LF on all platforms,
  so Windows and Linux checkouts are byte-identical.
- **libjpeg-turbo via ExternalProject.** libjpeg-turbo forbids
  `add_subdirectory` integration, so it is built as an isolated `ExternalProject`
  (SIMD off, static) and its static library (`jpeg-static.lib` /
  `libjpeg.a`) is linked in.
- **stb_image_write is intentionally not used** for saving — it buffers whole
  images in RAM, which is impossible at gigapixel sizes. All writers stream rows.
- **Scratch files** are created with `FILE_FLAG_DELETE_ON_CLOSE` on Windows and
  unlinked right after creation on Linux, so they are reclaimed automatically
  when the app exits (or a new render starts), even after a crash; the `temp`
  folder itself stays, empty.

## Project layout

```
CMakeLists.txt              build + all FetchContent dependencies + tests + CPack
cmake/embed_spirv.cmake     .spv  ->  embedded C header
.github/workflows/build.yml CI: Linux (Ubuntu 22.04) + Windows build, test, package
PORTING.md                  Linux port: analysis, decisions, plan, progress
shaders/pathtrace.comp      the path tracing kernel (USE_RAY_QUERY ifdef)
shaders/common.glsl         RNG, BSDFs, intersection, NEE/MIS, sky
fonts/                      bundled Roboto UI fonts (copied next to the exe)
src/main.cpp                entry point (portable main + Windows WinMain shim)
src/platform.h              OS interface: dialogs, message boxes, exe dir, files
src/platform_win32.cpp      Windows implementation (DPI, COM, IFileDialog)
src/platform_posix.cpp      Linux implementation (/proc/self/exe, zenity/kdialog dialogs)
src/ui.{h,cpp}              window, swapchain, ImGui, tabs, save
src/renderer.{h,cpp}        out-of-core tile scheduler, batching, recovery
src/viewer.{h,cpp}          fit/zoom/pan, preview + detail streaming
src/vk_context.{h,cpp}      instance/device/backend selection (volk, VMA)
src/accel.{h,cpp}           BLAS/TLAS for the hardware ray-query path
src/bvh.{h,cpp}             binned-SAH BVH for the compute path
src/scene.{h,cpp}           demo scene, materials, light list
src/obj_loader.{h,cpp}      OBJ/MTL import (any path, smooth normals)
src/mmap_image.{h,cpp}      disk-backed image: scratch folder, free space
src/mmap_image_win32.cpp    Windows file mapping (CreateFileMapping/MapViewOfFile)
src/mmap_image_posix.cpp    Linux file mapping (posix_fallocate + mmap)
src/savers.{h,cpp}          streaming PNG / JPEG / PPM
src/mathlib.h               compact vector/matrix math
src/render_settings.h       the exact resolution / spp / bounce tables
tests/test_*.cpp            doctest unit tests
tests/data/                 OBJ/MTL fixtures for the loader tests
tests/golden/               reference image for the self-test comparison
tests/tools/ppm_compare.cpp golden-image comparison tool
```
