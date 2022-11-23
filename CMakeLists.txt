cmake_minimum_required(VERSION 3.10)
project(Minesweeper)


################
# General build options

# Set a default build type if none was specified
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified. Defaulting to '${default_build_type}'.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
    # Choices for cmake-gui and ccmake
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug"
        "Release"
        "RelWithDebInfo")
endif()


################
# General compilation and tools

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_CLANG_TIDY
  "Enable static code analysis with clang-tidy"
  OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(CLANGTIDY)
    message(STATUS "Found clang-tidy: ${CLANGTIDY}")
    set(CMAKE_CXX_CLANG_TIDY
      ${CLANGTIDY};
      -header-filter=/src;  # Static analysis only for our headers
      -checks=-*,bugprone-*,clang-analyzer-*,cppcoreguidelines-*,modernize-*,performance-*,portability-*,readability-*,-cppcoreguidelines-avoid-magic-numbers,-readability-magic-numbers,-cppcoreguidelines-pro-type-vararg,-readability-isolate-declaration,-modernize-use-trailing-return-type,-modernize-use-nodiscard,-cppcoreguidelines-pro-bounds-pointer-arithmetic;)
  else()
    message(SEND_ERROR "clang-tidy was requested but not found")
  endif()
endif()
mark_as_advanced(FORCE CLANGTIDY)


################
# Compiler warnings, setup, etc.

# Per-compiler warning flags, optimizations, and other settings.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(compiler_warning_flags -Wall -Wextra -Wunreachable-code -Wpedantic -Wshadow -Wformat)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")  # Add debug info to debug build.
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")  # Optimize for speed.
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND compiler_warning_flags -Wweak-vtables -Wmissing-noreturn)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /W3 /permissive-")
    # Statically link the MSVC runtime library. This must be set *before* defining compile targets.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")  # Optimize for speed.
endif()
add_compile_options(${compiler_warning_flags})


# Increase stack size for bucket_reveal() on large grids.
if (MSVC)
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:25000000")
else ()
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=25000000")
endif()


################
# External libraries

if(UNIX)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
endif()
if(WIN32)
    # I can't get vcpkg to work with cmake, so we'll do this manually.
    set(external_packages
        "SDL2-2.0.22"
        "SDL2_image-2.6.0")
    foreach(dir IN LISTS external_packages)
        include_directories("external/windows/${dir}/include")
        link_directories("external/windows/${dir}/lib/x64")
    endforeach()
    include_directories("external/windows/lp_solve_5.5.2.11")
    link_directories("external/windows/lp_solve_5.5.2.11")
endif()


################
# Project executables

add_executable(Minesweeper WIN32 main.cpp)
if(UNIX)
    target_include_directories(
        Minesweeper PUBLIC
        ${SDL2_INCLUDE_DIRS})
endif()
target_link_libraries(Minesweeper PRIVATE SDL2 SDL2_image lpsolve55)


################
# Post-build

# Copy data/resources to build folder
add_custom_command(TARGET Minesweeper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:Minesweeper>/data)

# Copy DLLs to build folder after build.
if(WIN32)
    # Copying DLLs (see <https://stackoverflow.com/questions/10671916>).
    set(WIN32_DLLS
        "SDL2-2.0.22/lib/x64/SDL2.dll"
        "SDL2_image-2.6.0/lib/x64/SDL2_image.dll"
        "lp_solve_5.5.2.11/lpsolve55.dll"
        )
  foreach(F IN LISTS WIN32_DLLS)
    add_custom_command(TARGET Minesweeper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${PROJECT_SOURCE_DIR}/external/windows/${F}"
                              $<TARGET_FILE_DIR:Minesweeper>
        COMMENT "Copying ${F} to build directory.")
  endforeach()
endif()

if(UNIX)
  install(TARGETS ${main_exe} DESTINATION bin)
endif()
