cmake_minimum_required(VERSION 3.16)

project(oak_barrels
    VERSION 0.1.0
    DESCRIPTION "A neutral relay between hosts and whatever audio engine is installed"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release so a plain `cmake -S . -B build` produces something usable
# rather than an unoptimised debug binary.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Tracked text is ASCII only, and CI enforces it. Nothing here therefore needs
# an encoding declaration to be read correctly. This flag is a guard for the day
# someone slips a non-ASCII character in: without it MSVC decodes source using
# the machine's code page, so the same file can compile differently in different
# regions. GCC and Clang already assume UTF-8.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# The platform header for this family defines min and max as macros, which
# breaks std::min and std::max in every file that includes it. Defining these
# once here is better than rediscovering it in each file that needs a platform
# header, and better than writing the workaround three different ways.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# The relay station.
#
# Note what is absent: there is no find_package for any vendor SDK, no
# include directory added from an installation, and no link against anything
# external. The relay resolves everything it needs at run time by design; a
# configure-time dependency on somebody's SDK would defeat the whole point.
# ---------------------------------------------------------------------------
add_library(oak_barrels_core STATIC
    src/relay/binding/dynamic_library.cpp
    src/relay/discovery/module_format.cpp
    src/relay/discovery/scanner.cpp
    src/relay/probe/probing_backend.cpp
    src/relay/probe/profile.cpp
    src/relay/probe/prober.cpp
    src/relay/protocol/frame.cpp
    src/relay/protocol/json.cpp
    src/relay/transport/named_pipe_transport.cpp
    src/relay/transport/stdio_transport.cpp
    src/relay/transport/unix_socket_transport.cpp
    src/relay/session.cpp
)

target_include_directories(oak_barrels_core PUBLIC src)
target_compile_features(oak_barrels_core PUBLIC cxx_std_17)

# Resolving a symbol at run time is what this project does instead of linking,
# so the platform's own loader is the one dependency it genuinely has.
target_link_libraries(oak_barrels_core PUBLIC ${CMAKE_DL_LIBS})

add_executable(oak-barrels src/relay/main.cpp)
target_link_libraries(oak-barrels PRIVATE oak_barrels_core)

# The station reports its own version and nothing else's. This is not the fact
# axiom A2 forbids: A2 forbids baking in knowledge of the engine on the other
# end, not the station knowing its own build.
target_compile_definitions(oak-barrels PRIVATE OAK_BARRELS_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Warnings. Warnings are not errors here on purpose: contributors compile
# against many toolchains, and a pedantic warning on one is noise on another.
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(oak_barrels_core PRIVATE /W4 /permissive-)
    target_compile_options(oak-barrels PRIVATE /W4 /permissive-)
else()
    target_compile_options(oak_barrels_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(oak-barrels PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(OAK_BARRELS_BUILD_TESTS "Build the relay's own tests" ON)

if(OAK_BARRELS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS oak-barrels RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
