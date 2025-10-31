cmake_minimum_required(VERSION 3.16)

project(stego-c-practice C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Use PkgConfig to find GTK4
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

# Find PNG and JPEG libraries
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)

# Add include directories
include_directories(
    include
    third_party/tiny-aes
    ${PNG_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
)

# Define the source files
set(SOURCES
    src/main.c
    src/aes_wrapper.c
    src/batch.c
    src/gui_batch.c
    src/gui_main.c
    src/image_io.c
    src/metadata.c
    src/payload.c
    src/stego_core.c
    third_party/tiny-aes/aes.c
)

# Add the executable target
add_executable(stego-c-practice ${SOURCES})

# Add include directories from GTK4
target_include_directories(stego-c-practice PRIVATE ${GTK4_INCLUDE_DIRS})

# Add compiler flags from GTK4
target_compile_options(stego-c-practice PRIVATE ${GTK4_CFLAGS})

# Link against GTK4
target_link_libraries(stego-c-practice PRIVATE 
    ${GTK4_LIBRARIES}
    ${PNG_LIBRARIES}
    ${JPEG_LIBRARIES}
)

install(TARGETS stego-c-practice DESTINATION bin)
