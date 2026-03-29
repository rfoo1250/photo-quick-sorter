# Folder Structure

## assets/
Static files bundled with the application. Resolved at runtime relative to the executable path.

---

## build/
CMake output directory. Generated automatically — do not edit or commit.
The final executable lives at `build/Debug/PhotoQuickSorter.exe`.

---

## docs/
Developer documentation. Add any guides or reference material here.

---

## old/
Scratch/archive folder for old files kept for reference. Not part of the build.

---

## src/
All C++ source and header files. `src/` is the single include root — all `#include` paths are relative to it (e.g. `"core/ImageRepository.h"`).

### src/app/
Application entry point. Contains `PhotoQuickSorterApp` which initialises wxWidgets and creates the main frame.

### src/core/
Business logic, independent of UI.
- `ImageRepository` — scans a folder for images, holds metadata, supports save/load to TSV.
- `FolderLocations` — plain struct holding the base folder and two destination folder paths.

### src/panels/
wxPanel subclasses — one per screen.
- `MainMenuPanel` — folder path inputs and the Start Sorting button.
- `SortPhotosPanel` — the main sorting screen: image display, compass-rose action buttons, end-of-session review, crash recovery.

### src/ui/
Top-level window and auxiliary windows.
- `PhotoQuickSorterFrame` — the main `wxFrame`; owns both panels and switches between them.
- `LogWindow` — developer-only log console, shown in Debug builds only.

### src/utils/
Shared headers, no `.cpp` files.
- `config.h` — defines `DEBUG_MODE` (fallback if CMake hasn't set it).
- `logging.h` — `LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR` macros.

---
