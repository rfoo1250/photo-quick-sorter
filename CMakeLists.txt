cmake_minimum_required(VERSION 3.16)
project(PhotoQuickSorter VERSION 0.1.0 LANGUAGES CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# wxWidgets setup
set(wxWidgets_ROOT_DIR "C:/Libraries/wxWidgets-3.3.1")
set(wxWidgets_LIB_DIR "C:/Libraries/wxWidgets-3.3.1/lib/vc_x64_lib")
set(wxWidgets_USE_STATIC ON)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# Source files — all inside src/
set(SOURCES
    src/PhotoQuickSorterApp.cpp
    src/PhotoQuickSorterFrame.cpp
    src/MainMenuPanel.cpp
    src/SortPhotosPanel.cpp
    src/FolderLocations.cpp
)
# Header files  — all inside src/
set(HEADERS
    src/PhotoQuickSorterApp.h
    src/PhotoQuickSorterFrame.h
    src/MainMenuPanel.h
    src/SortPhotosPanel.h
    src/FolderLocations.h
)

# Create the executable (Windows GUI app)
add_executable(PhotoQuickSorter WIN32 ${SOURCES} ${HEADERS})

# add more stuff here on
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SOURCES} ${HEADERS})

target_compile_definitions(PhotoQuickSorter PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE=1>
    $<$<NOT:$<CONFIG:Debug>>:DEBUG_MODE=0>
)

# Link against wxWidgets
target_link_libraries(PhotoQuickSorter PRIVATE ${wxWidgets_LIBRARIES})

# Optional: organize IDE project structure (Visual Studio, CLion)
# source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SOURCES} ${HEADERS})

# Optional: add include directories for headers
target_include_directories(PhotoQuickSorter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
