cmake_minimum_required(VERSION 3.16)
project(PhotoQuickSorter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(wxWidgets_ROOT_DIR "C:/Libraries/wxWidgets-3.3.1")
set(wxWidgets_LIB_DIR "C:/Libraries/wxWidgets-3.3.1/lib/vc_x64_lib")

set(wxWidgets_USE_STATIC ON)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(PhotoQuickSorter WIN32 src/photo-quick-sorter.cpp)
target_link_libraries(PhotoQuickSorter ${wxWidgets_LIBRARIES})
