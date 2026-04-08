cmake_minimum_required(VERSION 3.16)
project(PhotoQuickSorter VERSION 0.1.0 LANGUAGES CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- wxWidgets setup ----
# adjust these paths to your installation if different
set(wxWidgets_ROOT_DIR "C:/Libraries/wxWidgets-3.3.1")
set(wxWidgets_LIB_DIR "C:/Libraries/wxWidgets-3.3.1/lib/vc_x64_lib")
set(wxWidgets_USE_STATIC ON)

# Prefer find_package to locate wxWidgets
find_package(wxWidgets REQUIRED COMPONENTS core base)

# include the helper file if present (older style)
if (DEFINED wxWidgets_USE_FILE)
    include(${wxWidgets_USE_FILE})
endif()

# ---- Source / headers ----
set(SOURCES
    src/app/PhotoQuickSorterApp.cpp
    src/ui/PhotoQuickSorterFrame.cpp
    src/ui/LogWindow.cpp
    src/ui/Toast.cpp
    src/ui/ThumbnailGrid.cpp
    src/core/FolderLocations.cpp
    src/core/ImageRepository.cpp
    src/panels/MainMenuPanel.cpp
    src/panels/SortPhotosPanel.cpp
)
set(HEADERS
    src/utils/config.h
    src/app/PhotoQuickSorterApp.h
    src/ui/PhotoQuickSorterFrame.h
    src/utils/logging.h
    src/ui/LogWindow.h
    src/ui/Toast.h
    src/ui/ThumbnailGrid.h
    src/core/FolderLocations.h
    src/core/ImageRepository.h
    src/panels/MainMenuPanel.h
    src/panels/SortPhotosPanel.h
)

add_executable(PhotoQuickSorter WIN32 ${SOURCES} ${HEADERS})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SOURCES} ${HEADERS})

target_compile_definitions(PhotoQuickSorter PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE=1>
    $<$<NOT:$<CONFIG:Debug>>:DEBUG_MODE=0>
)

# ---- Attach wxWidgets to the target (important) ----
# Add include directories found by find_package
if (DEFINED wxWidgets_INCLUDE_DIRS)
    target_include_directories(PhotoQuickSorter PRIVATE ${wxWidgets_INCLUDE_DIRS})
endif()

# Add any definitions needed by wxWidgets
if (DEFINED wxWidgets_DEFINITIONS)
    # wxWidgets_DEFINITIONS contains things like -D__WXMSW__ etc.
    # Use target_compile_definitions so they are applied to the target only.
    target_compile_definitions(PhotoQuickSorter PRIVATE ${wxWidgets_DEFINITIONS})
endif()

# Link against wxWidgets libraries discovered by find_package
target_link_libraries(PhotoQuickSorter PRIVATE ${wxWidgets_LIBRARIES})

# Windows Media Foundation (MFPlay) for video playback
target_link_libraries(PhotoQuickSorter PRIVATE mfplay.lib mfplat.lib mfuuid.lib)

# Link bundled wxWidgets third-party libs (not included by find_package automatically)
# These are required by wxmsw33ud_core.lib for image format support
target_link_libraries(PhotoQuickSorter PRIVATE
    $<$<CONFIG:Debug>:${wxWidgets_LIB_DIR}/wxwebpd.lib>
    $<$<CONFIG:Debug>:${wxWidgets_LIB_DIR}/wxjpegd.lib>
    $<$<CONFIG:Debug>:${wxWidgets_LIB_DIR}/wxpngd.lib>
    $<$<CONFIG:Debug>:${wxWidgets_LIB_DIR}/wxtiffd.lib>
    $<$<CONFIG:Debug>:${wxWidgets_LIB_DIR}/wxzlibd.lib>
    $<$<CONFIG:Debug>:${wxWidgets_LIB_DIR}/wxexpatd.lib>
)

# Optional: include your src folder for your own headers
target_include_directories(PhotoQuickSorter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
