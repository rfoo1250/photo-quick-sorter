# Developer Guide

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.16+ | |
| MSVC | VS 2019/2022 | x64 toolchain |
| wxWidgets | 3.3.1 | Static build |

wxWidgets must be built as a **static debug** library. Before building, update `wxWidgets_ROOT_DIR` and `wxWidgets_LIB_DIR` in [CMakeLists.txt](../CMakeLists.txt) to match your local installation path.

---

## Configure and Build

**In VS Code with the CMake extension:**

1. Open the repo folder.
2. Select kit: `Visual Studio Community 2022 Release - amd64` (or your MSVC x64 variant).
3. Select build type: `Debug`.
4. **CMake: Configure** — generates build files into `build/`.
5. **CMake: Build** (or `F7`) — compiles the executable.

**From the terminal:**
```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

> Only the **Debug** configuration is supported currently — the wxWidgets install only includes debug libs (`*d.lib`). A Release build will fail to link until Release libs are available.

---

## Output Executable

After a successful Debug build:
```
build/Debug/PhotoQuickSorter.exe
```

---

## Rebuild from Scratch

**CMake: Clean Rebuild** in VS Code, or from the terminal:
```bash
cmake --build build --config Debug --clean-first
```

To fully regenerate (e.g. after changing CMakeLists.txt):
```bash
rm -rf build/
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

---

## Debug Mode

`DEBUG_MODE` controls whether `LOG_DEBUG(...)` calls compile in and whether the log window appears.

**To switch, edit [src/utils/config.h](../src/utils/config.h)** and rebuild:

```cpp
#define DEBUG_MODE 1   // dev build — logs + log window visible
#define DEBUG_MODE 0   // release build — all debug output stripped
```

Always build with `--config Debug` (Release config requires wxWidgets Release libs which aren't set up):
```bash
cmake --build build --config Debug
```

---

## Logging

All logging goes through [src/utils/logging.h](../src/utils/logging.h). Logs appear in the in-app **Log Window** (developer tool, toggled from the menu).

| Macro | When active | Use for |
|-------|------------|---------|
| `LOG_DEBUG(...)` | `DEBUG_MODE=1` only | Verbose dev traces |
| `LOG_INFO(...)` | Always | Normal flow milestones |
| `LOG_WARN(...)` | Always | Non-fatal unexpected states |
| `LOG_ERROR(...)` | Always | Failures, bad input |

All macros accept `printf`-style format strings.

---

## Project Structure

```
src/
  app/       — WxApp entry point (PhotoQuickSorterApp)
  core/      — Business logic (ImageRepository, FolderLocations)
  panels/    — UI panels (MainMenuPanel, SortPhotosPanel)
  ui/        — Frame and auxiliary windows (PhotoQuickSorterFrame, LogWindow)
  utils/     — Shared utilities (config.h, logging.h)
assets/      — Static assets (logo image)
docs/        — Documentation
build/       — CMake output (git-ignored)
```

---

## Crash Recovery

During a sorting session, pending actions are written to three text files in the base folder:
- `_pending_folder1.txt`
- `_pending_folder2.txt`
- `_pending_deletes.txt`

If the app closes mid-session, reopening the same base folder will prompt to resume the review. These files are deleted automatically after the end-of-session review completes.
