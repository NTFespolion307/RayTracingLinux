# Porting VkGigaTracer to Linux

Status: plan approved 2026-10-15. **Steps 1–11 done.**
- Windows: builds, 42/42 tests pass.
- Linux (WSL distro `RayTracing`, Ubuntu 26.04, GCC 15): builds, 40/40 unit tests + 2/2 GPU tests pass under Xvfb with Mesa lavapipe, and the GUI starts. lavapipe exposes `VK_KHR_ray_query`, so the hardware ray-query backend was exercised for the first time; its image matches the Windows compute-path reference (MAE 0.000).
- Still to do on Linux: native file dialogs and message boxes (step 12; they currently act as "cancelled" / print to stderr), then steps 13–17.

Build host note: the WSL distro is Ubuntu **26.04**, not 24.04 as assumed in §0. A binary built there needs a glibc at least that new, so a release tarball meant for 24.04 users must be built on 24.04 (step 17).

Run the Linux build and tests (inside WSL, distro `RayTracing`): see Appendix A; the build tree lives in `~/vkgt`, outside the Windows checkout.

Extra fix found along the way: three pre-existing Vulkan validation errors (descriptor pool type, buffer usage, swapchain semaphores); the Debug build now runs with zero validation messages.

Run the tests (from the repo root, after building): `ctest --test-dir build -C Release --output-on-failure`. Add `-LE gpu` to skip the two tests that need a Vulkan GPU.

## 0. What the codebase is

| Item | Finding |
|---|---|
| Language | C++20 (+ C11 for vendored libs), GLSL compute shaders |
| Build | CMake ≥ 3.24, `FetchContent` for every dependency, MSVC / Visual Studio generator only |
| GPU | Vulkan only, loaded dynamically via **volk** (already cross-platform) |
| Window / input | **GLFW 3.4** (already cross-platform) |
| UI | **Dear ImGui 1.91.5** with GLFW + Vulkan backends (already cross-platform) |
| Image I/O | zlib, libpng, libjpeg-turbo, tinyobjloader (all portable) |
| Windows-specific | entry point, DPI, COM file dialogs, `MessageBoxA`, memory-mapped scratch file, wide-char paths and MSVC CRT calls, the Vulkan surface extension name, and parts of the CMake build |
| Linux target (inferred) | Ubuntu 24.04 x86_64. WSL2 Ubuntu-24.04 is installed on this machine and can serve as the first Linux build host |

The template's context placeholders were not filled in, so the values above come from the code. Correct them in §7 if they're wrong.

**Overall assessment:** this is a port, not a rewrite. The renderer, BVH, accel structures, viewer, theme, shaders, and threading (`std::thread`/`std::mutex`/`std::atomic`) are already portable. All Windows coupling sits in about 8 files, roughly 350 lines.

**Not found anywhere:** registry access, Windows services, sockets, process spawning, file locking, `__declspec`, MSVC intrinsics, `#pragma` other than `once`, `.bat`/`.ps1` scripts, installers, vcpkg/NuGet packages, or `%APPDATA%`-style locations.

---

## 1. Issue table

Severity:
- **blocker**: won't compile, link, or run on Linux.
- **needs change**: compiles, but behaves wrongly on Linux or will break once a blocker is fixed.
- **minor**: cosmetic, docs, or hygiene.

### 1.1 Build system

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| B1 | [CMakeLists.txt:177-185](CMakeLists.txt#L177-L185) | Downloads the **Windows embeddable Python** zip and points glslang at `python.exe` | blocker | `if(WIN32)` keep as is; else `find_package(Python3 REQUIRED COMPONENTS Interpreter)` (python3 ships with Ubuntu) |
| B2 | [CMakeLists.txt:161](CMakeLists.txt#L161), [:166-174](CMakeLists.txt#L166-L174) | Links libjpeg-turbo as `<cfg>/jpeg-static.lib`, assuming MSVC naming and a multi-config layout | blocker | Compute the path: on MSVC `jpeg-static.lib` in `<cfg>/`; elsewhere `libjpeg.a` with no config subdir (use `GENERATOR_IS_MULTI_CONFIG`). Pass `-DCMAKE_BUILD_TYPE` to the ExternalProject for single-config generators |
| B3 | [CMakeLists.txt:290-295](CMakeLists.txt#L290-L295) | Links `Dwmapi Shcore Shlwapi Ole32 Shell32` unconditionally | blocker | Wrap in `if(WIN32)`. On Linux link `Threads::Threads` and `${CMAKE_DL_LIBS}` (volk uses `dlopen`). `Dwmapi`, `Shcore`, and `Shlwapi` have **no call sites in `src/`**, so they can probably be dropped on Windows too (verify) |
| B4 | [CMakeLists.txt:23-27](CMakeLists.txt#L23-L27) | Under `if(MSVC)`, sets portable defines (`VK_NO_PROTOTYPES`) alongside Windows ones (`VK_USE_PLATFORM_WIN32_KHR`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE`) | needs change | Move `VK_NO_PROTOTYPES` out to all platforms. Keep the Win32 ones in `if(WIN32)`. Add a GCC/Clang block (`-Wall -Wextra`, optionally `-static-libstdc++ -static-libgcc` for a "copy and run" binary like the Windows one) |
| B5 | GLFW FetchContent ([:59-67](CMakeLists.txt#L59-L67)) | On Linux, GLFW 3.4 builds both X11 and Wayland backends and needs their **dev headers** | needs change (build env) | Document the apt packages (Appendix A). Optionally expose `GLFW_BUILD_WAYLAND`/`GLFW_BUILD_X11` toggles. GLFW 3.4 `dlopen`s the X11/Wayland libs at runtime, so the binary keeps no hard dependency on them |
| B6 | [CMakeLists.txt:16](CMakeLists.txt#L16), [:157](CMakeLists.txt#L157) | Static MSVC runtime | minor | Ignored on GCC, so harmless. The Linux equivalent is the `-static-libstdc++` from B4 |
| B7 | [CMakeLists.txt:273](CMakeLists.txt#L273) | `add_executable(... WIN32 ...)` | minor | Ignored off Windows; keep |
| B8 | repo root | No `.gitattributes`; `core.autocrlf=true`; index is LF, working tree is CRLF | minor | Add `.gitattributes` (`* text=auto eol=lf`, plus `*.ttf binary`) so WSL and Linux checkouts match. Nothing parses line endings at runtime: the PPM header is written in `"wb"` mode and glslang accepts CRLF |
| B9 | [README.md:34-52](README.md#L34-L52) | Build docs are Visual Studio only | minor | Add a Linux section |

### 1.2 Entry point, process setup, messages

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| S1 | [src/main.cpp:6-7](src/main.cpp#L6-L7) | `<windows.h>`, `<objbase.h>` | blocker | Move into `platform_win32.cpp` |
| S2 | [src/main.cpp:9](src/main.cpp#L9) | `WinMain` entry point | blocker | Portable `int main(int argc, char** argv)`, plus a `WinMain` shim under `#ifdef _WIN32` that forwards to it (keeps the `/SUBSYSTEM:WINDOWS` no-console behavior) |
| S3 | [src/main.cpp:11](src/main.cpp#L11) | `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` | blocker | `platform::initProcess()`: Windows unchanged; Linux no-op (GLFW reports the content scale) |
| S4 | [src/main.cpp:14](src/main.cpp#L14), [:26](src/main.cpp#L26) | COM init/uninit for `IFileDialog` | blocker | Inside `platform::initProcess()`/`shutdownProcess()`, Windows only |
| S5 | [src/main.cpp:19-20](src/main.cpp#L19-L20) | `GetCommandLineW` + `wcsstr` for `--selftest` | blocker | Scan `argv` |
| S6 | [src/ui.cpp:22-24](src/ui.cpp#L22-L24) and callers at [:112](src/ui.cpp#L112), [:116](src/ui.cpp#L116), [:130-132](src/ui.cpp#L130-L132), [:139](src/ui.cpp#L139), [:144-145](src/ui.cpp#L144-L145), [:340](src/ui.cpp#L340), [:397](src/ui.cpp#L397) | `MessageBoxA` with `MB_ICONERROR`/`MB_ICONWARNING` in the helper signature | blocker | `platform::showMessage(MsgKind::Error/Warning, text, title)`. Windows uses the same `MessageBoxA`; Linux uses the dialog library from S8, falling back to stderr plus the log |
| S7 | [src/ui.cpp:131](src/ui.cpp#L131) | Error text names `vulkan-1.dll` | minor | Platform-neutral wording ("the Vulkan loader") |

### 1.3 Native file dialogs

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| S8 | [src/ui.cpp:12-13](src/ui.cpp#L12-L13), [:39-105](src/ui.cpp#L39-L105) | COM `IFileOpenDialog` / `IFileSaveDialog` / `FOS_PICKFOLDERS` | blocker | `platform::openFileDialog / saveFileDialog / pickFolderDialog`. **Windows: keep this exact code** (moved verbatim). **Linux:** new code, see §3 and decision Q2 |
| S9 | [src/ui.cpp:410-416](src/ui.cpp#L410-L416), [:44](src/ui.cpp#L44), [:66](src/ui.cpp#L66) | `wchar_t` filter specs (`L"*.png"`) | needs change | Pass filters as UTF-8 `{desc, "*.png"}` and convert inside the Win32 implementation |

### 1.4 Disk-backed image (core feature)

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| M1 | [src/mmap_image.h:7](src/mmap_image.h#L7), [:43-44](src/mmap_image.h#L43-L44) | `<windows.h>` in a public header; `HANDLE` members | blocker | Hide handles behind `#ifdef _WIN32` (`HANDLE file_, mapping_` vs `int fd_`), or use a pimpl. Keeps `windows.h` out of every includer |
| M2 | [src/mmap_image.cpp:6-11](src/mmap_image.cpp#L6-L11) | `GetTempPathW` / `MAX_PATH` for the default scratch folder | blocker | Windows: `std::filesystem::temp_directory_path()`, which calls `GetTempPathW` and so behaves the same. Linux: `$TMPDIR`, else **`/var/tmp`, not `/tmp`**. `/tmp` is tmpfs (RAM-backed) on Fedora, Arch, and others, and a 12.4 GiB scratch file there would eat RAM and swap |
| M3 | [src/mmap_image.cpp:14-20](src/mmap_image.cpp#L14-L20) | `GetDiskFreeSpaceExW` | blocker | `std::filesystem::space(dir).available`. Portable, and uses `GetDiskFreeSpaceExW` on Windows and `statvfs` on Linux, so no `#ifdef` is needed |
| M4 | [src/mmap_image.cpp:29-30](src/mmap_image.cpp#L29-L30) | Appends `\\` as a separator | needs change | `std::filesystem::path / name` |
| M5 | [src/mmap_image.cpp:34-36](src/mmap_image.cpp#L34-L36) | `swprintf` + `GetCurrentProcessId()` for a unique name | blocker | `snprintf` + `platform::processId()` (`getpid()` on Linux) |
| M6 | [src/mmap_image.cpp:39-56](src/mmap_image.cpp#L39-L56) | `CreateFileW(FILE_FLAG_DELETE_ON_CLOSE \| FILE_ATTRIBUTE_TEMPORARY)`, `CreateFileMappingW`, 64-bit `MapViewOfFile` | blocker | POSIX backend: `open(O_RDWR\|O_CREAT\|O_EXCL\|O_CLOEXEC, 0600)`, then **`unlink()` right away** (the equivalent of delete-on-close: space is reclaimed even after a crash), then **`posix_fallocate(fd, 0, size)`**, then `mmap(PROT_READ\|PROT_WRITE, MAP_SHARED)`. ⚠ Do **not** use plain `ftruncate`. That creates a *sparse* file, and if the disk fills mid-render, writes through the mapping raise **SIGBUS** and kill the process. Windows reserves the space up front; `posix_fallocate` matches that |
| M7 | [src/mmap_image.cpp:58-64](src/mmap_image.cpp#L58-L64) | `UnmapViewOfFile` / `CloseHandle` | blocker | `munmap` / `close` |
| M8 | [src/renderer.cpp:422](src/renderer.cpp#L422) | `memset`s the whole mapping to zero (12.4 GiB at 80K) | minor (not a porting issue) | Freshly extended or fallocated files already read as zero on both OSes. Leave it alone during the port and revisit separately (it costs startup time at 40K/80K) |

### 1.5 Paths, strings, CRT

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| P1 | [src/render_settings.h:57](src/render_settings.h#L57), [src/mmap_image.h:19,32,38,40,48](src/mmap_image.h#L19), [src/savers.h:21-26](src/savers.h#L21-L26), [src/obj_loader.h:14](src/obj_loader.h#L14), [src/ui.h:103](src/ui.h#L103), [src/renderer.h:40](src/renderer.h#L40) | `std::wstring` used as the path type throughout | needs change | **`std::filesystem::path`**. It stores `wchar_t` on Windows, so non-ASCII path support is preserved, and UTF-8 `char` on Linux. `errMsg` in `renderer.h:40` becomes a UTF-8 `std::string` |
| P2 | [src/savers.cpp:14-16](src/savers.cpp#L14-L16) | `_wfopen` | blocker | `platform::fopenPath(const fs::path&, mode)`: `_wfopen(p.c_str())` on Windows, `fopen(p.c_str())` on POSIX |
| P3 | [src/savers.cpp:27](src/savers.cpp#L27) | `_wremove` | blocker | `std::filesystem::remove(p, ec)` |
| P4 | [src/vk_context.cpp:26](src/vk_context.cpp#L26) | `_wfopen` for the log | blocker | `platform::fopenPath` |
| P5 | [src/vk_context.cpp:31](src/vk_context.cpp#L31) | `localtime_s(&tm, &t)` (MSVC argument order; not in glibc) | blocker | Small wrapper: `localtime_s` on Windows, `localtime_r` on POSIX |
| P6 | [src/theme.cpp:85](src/theme.cpp#L85) | `fopen_s` | blocker | `std::filesystem::exists` (it only checks existence) |
| P7 | [src/obj_loader.cpp:45](src/obj_loader.cpp#L45), [:99](src/obj_loader.cpp#L99) | `std::ifstream(const wchar_t*)`, an **MSVC-only extension** | blocker | `std::ifstream(const fs::path&)` is standard on both |
| P8 | [src/obj_loader.cpp:5](src/obj_loader.cpp#L5), [:13-27](src/obj_loader.cpp#L13-L27); [src/ui.cpp:26-32](src/ui.cpp#L26-L32) | `WideCharToMultiByte` / `MultiByteToWideChar` (duplicated in two files) | blocker | Unneeded once P1 is done: `fs::path(u8string)` and a single `platform::toUtf8(const fs::path&)` helper |
| P9 | [src/obj_loader.cpp:30-34](src/obj_loader.cpp#L30-L34), [:120](src/obj_loader.cpp#L120) | `dirOf()` splits on `\\` or `/` | needs change | `path.parent_path()` / `path.filename()`. On Linux `\` is a legal filename character |
| P10 | [src/obj_loader.cpp:44](src/obj_loader.cpp#L44) | Resolves `mtllib` names relative to the OBJ | needs change | OBJ files authored on Windows often say `mtllib Textures\Foo.MTL` with backslashes and the wrong case. On Linux, normalize `\` to `/` and, if the exact name is missing, fall back to a case-insensitive match in that directory. Windows behavior is unchanged |
| P11 | [src/ui.cpp:969](src/ui.cpp#L969) | `Sleep(100)` | blocker | `std::this_thread::sleep_for(100ms)` |
| P12 | [src/obj_loader.cpp:42](src/obj_loader.cpp#L42), [:69](src/obj_loader.cpp#L69), [:81](src/obj_loader.cpp#L81) | Uses `std::map`, `std::max`, `std::sqrt` without including `<map>`/`<algorithm>`/`<cmath>` directly (relies on transitive includes) | minor | Add the explicit includes if GCC complains. In general, expect a handful of missing-include errors on first GCC compile, because MSVC's headers pull in more transitively |

### 1.6 Executable-relative resources (fonts, log)

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| R1 | [src/theme.cpp:6](src/theme.cpp#L6), [:97-103](src/theme.cpp#L97-L103) | `GetModuleFileNameA` (ANSI, so it is also a latent Windows bug for non-ASCII install paths) | blocker | `platform::exeDir()`: `GetModuleFileNameW` on Windows, `readlink("/proc/self/exe")` on Linux |
| R2 | [src/vk_context.cpp:2](src/vk_context.cpp#L2), [:14-21](src/vk_context.cpp#L14-L21) | `GetModuleFileNameW` / `MAX_PATH` to put `gigatracer.log` next to the exe | blocker | `platform::exeDir()`. On Linux, if the exe dir isn't writable (installed to `/usr/bin`), fall back to `$XDG_STATE_HOME/VkGigaTracer/` (decision Q4) |
| R3 | [src/theme.cpp:112](src/theme.cpp#L112) | Builds `dir + "fonts\\" + file` | needs change | On Linux this creates a filename containing a backslash, so the bundled Roboto is silently skipped. Use `exeDir() / "fonts" / file` |
| R4 | [src/theme.cpp:126-127](src/theme.cpp#L126-L127) | Falls back to `C:\Windows\Fonts\segoeui.ttf` / `seguisb.ttf` | needs change | Per-platform fallback list. Linux: `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` (and `-Bold`), then ImGui's built-in font, which the code already supports |
| R5 | [src/ui.cpp:978](src/ui.cpp#L978) | Self-test writes `L"selftest_out.ppm"` relative to **cwd** (the README says "next to the exe") | needs change | Becomes `fs::path`. Decide whether code or README is correct (Q8) |

### 1.7 Vulkan / windowing

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| V1 | [src/vk_context.cpp:83-86](src/vk_context.cpp#L83-L86) | Hardcodes the `"VK_KHR_win32_surface"` instance extension | blocker | `glfwGetRequiredInstanceExtensions()` returns `VK_KHR_xcb_surface`/`xlib` on X11, `VK_KHR_wayland_surface` on Wayland, and win32 on Windows |
| V2 | [src/vk_context.cpp:120](src/vk_context.cpp#L120) | Validation retry hardcodes `enabledExtensionCount = 2` | needs change | Becomes wrong once V1 makes the list dynamic. Remember the count of required extensions instead |
| V3 | [src/ui.cpp:136](src/ui.cpp#L136) | `glfwInitVulkanLoader` is called **after** the instance is created | needs change | Call it right after `volkInitialize()` and before V1's query, so GLFW and volk share one loader (`libvulkan.so.1`) |
| V4 | [src/ui.cpp:316-323](src/ui.cpp#L316-L323) | Bakes `glfwGetWindowContentScale` into font sizes and style | needs change | On Windows, window size equals framebuffer size. On **Wayland** with fractional or 2× scaling, window coordinates are logical and ImGui already applies `DisplayFramebufferScale`, so baking the scale into fonts **double-scales the UI**. Derive the UI scale from `framebuffer / window` size, or only bake it on platforms where the two are equal. Also handle `glfwSetWindowContentScaleCallback` for monitor changes |
| V5 | [src/ui.cpp:248](src/ui.cpp#L248) | `compositeAlpha = OPAQUE_BIT` hardcoded | minor | Pick from `caps.supportedCompositeAlpha` (some Wayland drivers advertise only `PRE_MULTIPLIED`/`INHERIT`) |
| V6 | [src/vk_context.cpp:175-190](src/vk_context.cpp#L175-L190) | Device selection requires a present-capable queue, so the self-test needs a real surface | needs change (testing) | The headless self-test on Linux CI needs Xvfb (`GLFW_PLATFORM=x11`) plus Mesa **lavapipe**. Longer term, a true surfaceless self-test path (decision Q7) |
| V7 | [src/renderer.h:3](src/renderer.h#L3), [README.md:153](README.md#L153) | "TDR safety" (~100 ms submits) | no change | Linux has equivalent GPU hang watchdogs (amdgpu job timeout, i915 hangcheck, NVIDIA watchdog on display GPUs). Keep the existing batching; only the README wording needs generalizing |

### 1.8 Repo hygiene

| # | File:line | What it does | Severity | Cross-platform replacement |
|---|---|---|---|---|
| H1 | [imgui.ini](imgui.ini) | Tracked in git even though `.gitignore` lists it | minor | `git rm --cached imgui.ini` |
| H2 | [README.md:209-211](README.md#L209-L211) vs [src/ui.cpp:960](src/ui.cpp#L960) | README says the self-test uses 16 spp; the code uses 64 | minor | Align (Q8) |

---

## 2. Recommended strategy

### 2.1 Thin platform layer and `std::filesystem` (recommended)

```
src/platform.h            // ~15 free functions, no OS headers
src/platform_win32.cpp    // existing Win32 code, moved verbatim
src/platform_posix.cpp    // new Linux implementation
src/mmap_image_win32.cpp  // existing CreateFileMapping code, moved verbatim
src/mmap_image_posix.cpp  // new: open/unlink/posix_fallocate/mmap
```

CMake selects the `*_win32.cpp` or `*_posix.cpp` files with `if(WIN32)`. `platform.h` exposes:

- `initProcess()` / `shutdownProcess()` (DPI and COM on Windows; no-op on Linux)
- `exeDir()`, `processId()`, `fopenPath()`, `localTime()`, `toUtf8(path)`
- `showMessage(kind, text, title)`
- `openFileDialog(filters)`, `saveFileDialog(filter, defExt)`, `pickFolderDialog()`
- `defaultScratchDir()`, `fallbackUiFonts()`

Use `std::filesystem` wherever it already abstracts the OS (temp dir, free space, remove, path joining), so those spots need no `#ifdef`.

**Why this approach:** Windows behavior stays byte-for-byte the same because the Win32 code is moved, not rewritten. The `#ifdef`s live in two or three files. The portable core (renderer, viewer, savers, OBJ loader) loses every OS include.

### 2.2 Alternatives considered

| Option | Pros | Cons |
|---|---|---|
| **A. Thin platform layer** (recommended) | Lowest risk to Windows; small diff; no new runtime dependencies on Windows | You maintain two small backends |
| B. Inline `#ifdef _WIN32` at each call site | Fastest to write | `windows.h` stays in shared headers; about 30 scattered ifdefs; hard to test |
| C. Replace GLFW with **SDL3** (gives windows, message boxes, file dialogs, and paths in one library) | One dependency covers everything; SDL3 dialogs use portals on Linux | Rewrites the window/input/ImGui-backend layer and touches DPI and swapchain code that works today, which risks Windows regressions. Far larger than the port needs |

### 2.3 Path type

Migrate every `std::wstring` path to `std::filesystem::path`, and every user-facing message to UTF-8 `std::string`. Convert to UTF-8 only at ImGui display boundaries, through `platform::toUtf8`. C++20's `u8string()` returns `std::u8string`, so the helper hides that cast.

### 2.4 Linux file dialogs (decision Q2)

| Option | Build dependencies | Runtime dependencies | Notes |
|---|---|---|---|
| **portable-file-dialogs** (header-only) | none | `zenity` or `kdialog` | Simplest. Spawns a helper process. On failure, fall back to an ImGui text-input path box |
| nativefiledialog-extended, portal backend | `libdbus-1-dev` | `xdg-desktop-portal` | Most native on GNOME/KDE/Flatpak. Usually missing under WSLg or bare window managers |
| nativefiledialog-extended, GTK3 backend | `libgtk-3-dev` | GTK3 | Heavy, and drags GTK into the process |

**Recommendation:** portable-file-dialogs on Linux only, with the ImGui path-box fallback. Windows keeps its existing `IFileDialog` code.

---

## 3. Parts that are new code rather than a port

Nothing needs a full rewrite. These pieces are new Linux-side implementations:

| Component | Work | Estimate |
|---|---|---|
| `mmap_image_posix.cpp` (M1–M7) | open/unlink/fallocate/mmap/statvfs, error paths, tmpfs-aware default dir | 0.5 day including tests |
| Linux dialogs and message box (S6, S8) | portable-file-dialogs integration plus ImGui fallback | 0.5 day |
| HiDPI rework (V4) | Separate logical scale from framebuffer scale; test on Windows at 100/150% and Wayland at 1×/2× | 0.5–1 day (the riskiest UI item) |
| CMake (B1–B5) | Conditional Python, jpeg-turbo naming, link libraries, flags | 0.5 day |
| Platform layer and path migration (S1–S5, P1–P11, R1–R5, V1–V3) | Mostly mechanical | 1–1.5 days |
| Tests before refactoring (§5) | Unit tests and golden-image harness | 1 day |
| **Total** | | **≈ 4–5 days** |

---

## 4. Implementation plan

Every step ends with: **Windows build passes and all tests pass**. From step 9 on, the **Linux (WSL) build and tests** must pass as well. Each step is a separate commit.

1. **Lock in behavior (Windows only, no app source changes).** Add `tests/` plus a CTest target and the §5 test suite; generate the golden self-test reference. *Verify:* `ctest` all green on Windows.
2. **Repo hygiene.** Add `.gitattributes`; untrack `imgui.ini`. *Verify:* `git status` clean after renormalize; build unchanged.
3. **CMake portability (B1–B4).** Make the Python, jpeg-turbo, and link-library blocks conditional, and move `VK_NO_PROTOTYPES`. Still Windows-only in practice. *Verify:* clean Windows rebuild; check that the exe's imports haven't grown (`dumpbin /dependents`); tests green.
4. **Introduce `platform.h` and `platform_win32.cpp`**, moving the existing code verbatim (S1–S6, S8, R1, R2), with the `main()` + `WinMain` shim. *Verify:* tests green; manually check the open, save, and folder dialogs plus the disk-space warning box.
5. **Path type migration (P1, P8, P9, S9).** Replace `wstring` with `fs::path` everywhere. *Verify:* tests green, including the non-ASCII path tests from step 1.
6. **CRT portability (P2–P7, P11).** *Verify:* tests green.
7. **Vulkan surface extensions (V1–V3).** *Verify:* tests green; validation-layer run (Debug) is clean.
8. **Split `MmapImage` into `_win32` and `_posix` files (M1).** Move the Win32 code verbatim, and use `fs::space`/`temp_directory_path` (M2, M3). *Verify:* MmapImage tests green on Windows.
9. **First Linux build in WSL.** Write `platform_posix.cpp` and `mmap_image_posix.cpp`, using stubbed dialogs (return empty) and stderr messages, and fix GCC errors such as the missing includes from P12. *Verify:* builds on Ubuntu 24.04 with GCC; CPU unit tests green on Linux.
10. **Linux self-test.** Run `--selftest` under Xvfb with lavapipe and compare to the golden image within tolerance. *Verify:* golden test green on Linux.
11. **Fonts and log location (R3, R4, R2 fallback).** *Verify:* Roboto loads from `build/fonts/` on Linux; log written.
12. **Linux dialogs and message boxes (S6, S8)** using the chosen library and ImGui fallback. *Verify:* manual open, save, and folder pick in WSLg.
13. **OBJ `mtllib` normalization (P10).** *Verify:* new test with a `Textures\Foo.MTL` reference passes on both OSes.
14. **HiDPI (V4) and composite alpha (V5).** *Verify:* visual check on Windows at 100% and 150%, on X11, and on Wayland at scale 2.
15. **Docs:** README Linux section; generalize the TDR and DLL wording.
16. *(Optional, Q6)* **CI:** GitHub Actions matrix (`windows-latest` and `ubuntu-24.04`) running build, unit tests, and the self-test (Xvfb + lavapipe on Linux).
17. *(Optional, Q3)* **Packaging:** AppImage or tarball.

---

## 5. Tests

### 5.1 What exists today

- **No unit tests, no `tests/` directory, no CTest.**
- `VkGigaTracer.exe --selftest` ([src/ui.cpp:951-984](src/ui.cpp#L951-L984)) renders the demo scene at 1080p and 64 spp to `selftest_out.ppm` and returns 0 or 1. It only checks that the render finished and a file was written. **It does not check any pixels.**

### 5.2 Tests to add before refactoring (step 1)

Framework: **doctest** through FetchContent (single header, fast), in a `VkGigaTracerTests` executable that links the CPU-only sources. To keep the tests source-compatible through the `wstring` → `fs::path` migration, build paths as `fs::path` and pass `.wstring()`. Step 5 then deletes the `.wstring()` calls.

| ID | Area | What it locks in |
|---|---|---|
| T1 | `render_settings.h` | Exact resolution table (labels, W×H), spp list, bounce list, tile sizes, defaults |
| T2 | `MmapImage` | Small odd-sized image (37×19): `valid`, `sizeBytes`, row stride, write-then-read pattern; scratch file **exists while open and is gone after `destroy()`**; empty folder resolves to an existing temp dir; `freeSpace` > 0; creating in a nonexistent folder fails cleanly; 2 GiB+ size math (`requiredBytes(76800, 43200)`) |
| T3 | `savers` | PPM: byte-exact header and RGB payload for a known pattern. PNG: decode with libpng and compare pixels exactly. JPEG: decode, check dimensions and mean error under a threshold. Setting `cancel` deletes the partial file and returns false. An unwritable path returns false with a message. A **non-ASCII filename** (`tëst_日本.ppm`) works |
| T4 | `obj_loader` | Fixture OBJ and MTL with Lambert, metal, glass, and emissive materials: triangle count and each `GPUMaterial` field. Missing MTL is non-fatal. Missing OBJ gives the error message. OBJ without normals gets smoothed normals. `scene.name`. **Non-ASCII directory** |
| T5 | `scene` | Demo scene: triangle, material, and emissive-light counts, plus camera defaults (snapshot) |
| T6 | `bvh` | Every triangle referenced exactly once; parent bounds contain child bounds; 10k random rays hit the same closest triangle and t as brute-force Möller–Trumbore |
| T7 | GPU layouts | `static_assert`/test on `sizeof` and `offsetof` of the GPU-facing structs in `scene.h`, so GCC and MSVC produce identical std430 layouts |
| T8 | Golden render | CTest runs `--selftest`, then a small C++ `ppm_compare` tool compares the output with a reference generated **now on Windows**. Compare with tolerance (mean absolute error and PSNR ≥ ~35 dB, not a hash), because floating-point results differ across GPUs and drivers. Also assert exit code 0 and `selftest: done` in the log |

Added later, alongside the code they cover:
- `platform::*` tests: `exeDir` exists, `defaultScratchDir` exists and is not tmpfs on Linux, UTF-8 round-trip.
- P10 `mtllib` backslash and case test.

---

## 6. Risks worth calling out

1. **SIGBUS on a full disk** if the POSIX mapping is sparse. Use `posix_fallocate` (M6).
2. **tmpfs scratch:** the default must not be `/tmp` (M2). Also show the resolved scratch path in the UI.
3. **Wayland HiDPI double-scaling** (V4). This is the one change that touches shared UI code; test it on Windows too.
4. **WSLg is not a performance or ray-query test bed.** Its Vulkan driver (Mesa "dozen" over D3D12, or lavapipe) is fine for build, unit, and compute-path tests. The hardware ray-query path needs native Linux with an RTX, RX 6000+, or Arc GPU.

---

## 7. Decisions (2026-10-15)

| # | Decision | Effect on the plan |
|---|---|---|
| Q1 | **X11 only for now**; Wayland later | Build GLFW with `GLFW_BUILD_WAYLAND=OFF` (fewer build deps). On Wayland desktops the app runs through XWayland. V4 (HiDPI double-scaling) drops to minor/deferred because X11 reports scale like Windows does. V5 is deferred |
| Q2 | **portable-file-dialogs** on Linux | Runtime needs `zenity` or `kdialog`; ImGui path-box fallback when neither is present |
| Q3 | **.tar.gz single binary** (exe + `fonts/`) | Static libstdc++/libgcc; built on Ubuntu 24.04 (glibc 2.39 minimum); needs `libvulkan1` at runtime. Step 17 becomes a tarball script |
| Q4 | Runtime files live **next to the executable**; scratch defaults to **`<exe dir>/temp/`** | Applies to **both Windows and Linux** (Windows changes from `%TEMP%`). If `<exe dir>/temp/` can't be created or written, fall back to the system temp dir (`%TEMP%` / `/var/tmp`). Replaces M2's `/var/tmp` default and R2's XDG fallback, and avoids tmpfs automatically. Add tests: default resolves to `<exe dir>/temp`; read-only exe dir falls back |
| Q5 | New repo **https://github.com/NTFespolion307/RayTracingLinux.git** (empty) for the cross-platform version | Add as a second remote; pushing only with explicit go-ahead |
| Q6 | **Xvfb** for the headless Linux self-test | No change to device selection. CI runs `xvfb-run ./VkGigaTracer --selftest` with lavapipe |
| Q7 | Self-test uses **16 spp** (README is correct) | One-line change ([src/ui.cpp:960](src/ui.cpp#L960)) from 64 to 16, made in step 1 **before** the T8 reference image is generated, so the reference is at 16 spp |
| Q8 | Drop `Dwmapi`, `Shcore`, `Shlwapi` **if not required** | In step 3: remove them, then do a clean Windows link and run tests. Restore any library the linker asks for |
| Q9 | **doctest** via FetchContent | Separate `VkGigaTracerTests` exe, run via `ctest` |

## 7a. Original questions

1. **Target confirmation:** Ubuntu 24.04 x86_64, using GCC 13 (the distro default)? Must it run under **both X11 and Wayland**, or is X11/XWayland enough for now?
2. **Linux file dialogs:** portable-file-dialogs (recommended; needs `zenity`/`kdialog` at runtime) or nativefiledialog-extended with the portal backend?
3. **Distribution:** "copy one binary and run" like Windows (static libstdc++; depends on glibc + `libvulkan.so.1`), an **AppImage**, or a `.deb`?
4. **Log and fonts location on Linux:** keep "next to the executable", with an XDG fallback when that isn't writable? Or always use XDG dirs (`~/.local/state/VkGigaTracer/`)? Windows stays next to the exe either way.
5. **Default scratch dir on Linux:** `$TMPDIR`, else `/var/tmp`? Or `$XDG_CACHE_HOME/VkGigaTracer`?
6. **CI:** do you want a GitHub Actions matrix (Windows + Ubuntu) as part of this work? Is there a GitHub remote?
7. **Headless self-test:** OK to require Xvfb for the Linux self-test? Or should I add a surfaceless device-selection path, which is a small change to `pickPhysicalDevice`, so the self-test runs without any display?
8. **Self-test discrepancies:** the README says 16 spp and output "next to the exe"; the code uses 64 spp and writes to the cwd. Which is intended? I'll lock in whichever you choose in T8.
9. **Dead Windows link libraries:** OK to drop `Dwmapi`, `Shcore`, and `Shlwapi` (no call sites) in step 3?
10. **Test framework:** doctest via FetchContent OK?

---

## Appendix A. Linux build and test (WSL distro `RayTracing`)

The distro already has the packages installed (`build-essential cmake
ninja-build pkg-config xorg-dev libxkbcommon-dev libvulkan1 mesa-vulkan-drivers
vulkan-tools xvfb zenity`). Run every command inside WSL.

1. In a Windows terminal, open the distro: `wsl -d RayTracing`. Success: a `raytracing@…$` prompt.
2. Configure (any directory): `cmake -S /mnt/c/Users/Admin/Documents/Code/RayTracing -B ~/vkgt/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_BASE_DIR=$HOME/vkgt/deps`. Success: ends with `Build files have been written to: /home/raytracing/vkgt/build`.
3. Build: `cmake --build ~/vkgt/build --parallel`. Success: `~/vkgt/build/VkGigaTracer` exists.
4. Unit tests: `ctest --test-dir ~/vkgt/build -LE gpu --output-on-failure`. Success: `100% tests passed, 0 tests failed out of 40`.
5. GPU tests on a virtual display: `xvfb-run -a ctest --test-dir ~/vkgt/build -L gpu --output-on-failure`. Success: `100% tests passed, 0 tests failed out of 2`.
6. Run the app (opens on the Windows desktop through WSLg): `~/vkgt/build/VkGigaTracer`. Success: the VkGigaTracer window appears.

Why this layout: the build tree and the dependency cache (`FETCHCONTENT_BASE_DIR`) live in the Linux filesystem, so they never collide with the Windows build's `build/` and `_deps_cache/`, and builds are much faster than under `/mnt/c`. The only Vulkan device in WSL is Mesa's lavapipe (CPU), so rendering is slower than on the GPU.
