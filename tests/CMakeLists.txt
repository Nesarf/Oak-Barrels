# The relay's own tests. No third-party test framework: the harness below is
# a few dozen lines, and a relay that vendors a framework has one more thing
# to explain at audit time.

# A stand-in engine, so the probe and bind path has something real to resolve.
# It lives in the test tree, and its symbol names are ours: the relay ships no
# knowledge of any product, and a profile names these at run time.
add_library(oak_probe_fixture SHARED fixtures/probe_fixture.cpp)
set_target_properties(oak_probe_fixture PROPERTIES OUTPUT_NAME "oak-probe-fixture")

if(MSVC)
    target_compile_options(oak_probe_fixture PRIVATE /W4 /permissive-)
else()
    target_compile_options(oak_probe_fixture PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(relay_tests
    harness.hpp
    test_main.cpp
    test_json.cpp
    test_frame.cpp
    test_module_format.cpp
    test_scanner.cpp
    test_profile.cpp
    test_prober.cpp
    test_bulk.cpp
    test_transport.cpp
    test_session.cpp
)

target_link_libraries(relay_tests PRIVATE oak_barrels_core)
target_include_directories(relay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# A generator expression rather than a literal path, so the fixture is found
# across build types, output directories and platforms without guessing.
target_compile_definitions(relay_tests PRIVATE
    OAK_PROBE_FIXTURE_PATH="$<TARGET_FILE:oak_probe_fixture>")
add_dependencies(relay_tests oak_probe_fixture)

if(MSVC)
    target_compile_options(relay_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(relay_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME relay_tests COMMAND relay_tests)

# The end-to-end suite needs the same fixture, because a station that has been
# told how to talk to something should be shown doing it.
set_tests_properties(relay_tests PROPERTIES
    ENVIRONMENT "OAK_PROBE_FIXTURE_PATH=$<TARGET_FILE:oak_probe_fixture>")
