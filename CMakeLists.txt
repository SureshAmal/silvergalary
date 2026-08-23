cmake_minimum_required(VERSION 3.15)
project(SilverGallerySuite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/easyexif
    ${CMAKE_CURRENT_SOURCE_DIR}/include/GLFW
    ${CMAKE_CURRENT_SOURCE_DIR}/viewer_linux
    ${CMAKE_CURRENT_SOURCE_DIR}/gallery_linux
)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

if (WIN32)
    set(EXTRA_LIBS opengl32)
    # On Windows, try to find installed or bundled GLFW
    find_package(glfw3 QUIET)
    if (NOT glfw3_FOUND)
        set(EXTRA_LIBS ${EXTRA_LIBS} glfw3)
    else()
        set(EXTRA_LIBS ${EXTRA_LIBS} glfw)
    endif()
else()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/libglfw3.a")
        set(GLFW_LIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/libglfw3.a")
    else()
        find_package(glfw3 REQUIRED)
        set(GLFW_LIB glfw)
    endif()
    set(EXTRA_LIBS ${GLFW_LIB} OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ==================== Silver Viewer ====================
add_executable(silver_viewer
    viewer_linux/main.cpp
    include/easyexif/exif.cpp
)
target_link_libraries(silver_viewer PRIVATE ${EXTRA_LIBS})

# ==================== Silver Gallery ====================
add_executable(silver_gallery
    gallery_linux/main.cpp
    include/sqlite3.c
    include/easyexif/exif.cpp
)
target_link_libraries(silver_gallery PRIVATE ${EXTRA_LIBS})

# Output to bin/
set_target_properties(silver_viewer silver_gallery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
