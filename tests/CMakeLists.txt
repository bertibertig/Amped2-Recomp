# Host tests: no game data, no generated code, no controller. Configure with
# -DAMPED2_TESTS=ON, then: ctest --test-dir <build> --output-on-failure

# Settings file: defaults, parsing, environment overrides, save/load.
add_executable(settings_test settings_test.c ${PROJECT_SOURCE_DIR}/src/settings/settings.c)
target_include_directories(settings_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
if(NOT WIN32)
    target_link_libraries(settings_test PRIVATE m)
endif()
add_test(NAME settings COMMAND settings_test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Controller layer against SDL's virtual pad (scripts/test_pad.sh). POSIX
# only: on Windows host_pad.c reads XInput, which has no virtual device.
if(NOT WIN32)
    find_package(SDL2 REQUIRED)
    set(RT ${XBOXRECOMP_DIR}/src)
    add_executable(pad_virtual pad_virtual.c
        ${PROJECT_SOURCE_DIR}/src/input/host_pad.c
        ${PROJECT_SOURCE_DIR}/src/input/amped2_keys.c
        ${RT}/input/keyboard.c)
    target_include_directories(pad_virtual PRIVATE ${RT}/input ${RT})
    target_link_libraries(pad_virtual PRIVATE SDL2::SDL2)
    add_test(NAME pad_virtual COMMAND pad_virtual)
    set_tests_properties(pad_virtual PROPERTIES ENVIRONMENT SDL_VIDEODRIVER=dummy)
endif()
