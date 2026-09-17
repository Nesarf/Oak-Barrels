# The relay's own tests. No third-party test framework: the harness below is
# a few dozen lines, and a relay that vendors a framework has one more thing
# to explain at audit time.
add_executable(relay_tests
    harness.hpp
    test_main.cpp
    test_json.cpp
    test_frame.cpp
    test_session.cpp
)

target_link_libraries(relay_tests PRIVATE oak_barrels_core)
target_include_directories(relay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(relay_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(relay_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME relay_tests COMMAND relay_tests)
