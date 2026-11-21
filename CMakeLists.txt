cmake_minimum_required(VERSION 3.19)

project(cinnawm_switcher VERSION 1.0.0)

option(BUILD_PLUGIN "Build native Albert plugin shared library" ON)
include(CTest)

if(BUILD_PLUGIN)
    find_package(Albert 35.1 REQUIRED)
    find_package(X11 REQUIRED)

    albert_plugin(
        QT Core DBus Gui Widgets
        SOURCES
            src/cinnawm_core.h
            src/cinnawm_core.cpp
            src/x11_keystate.h
            src/x11_keystate.cpp
            src/plugin.h
            src/plugin.cpp
        LINK PRIVATE X11::X11
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Core Test)
    set(CMAKE_AUTOMOC ON)

    add_executable(test_albert_native
        tests/test_albert_native.cpp
        src/cinnawm_core.h
        src/cinnawm_core.cpp
    )
    target_compile_features(test_albert_native PRIVATE cxx_std_23)
    target_compile_definitions(test_albert_native PRIVATE QT_NO_CAST_FROM_ASCII)
    target_compile_options(test_albert_native PRIVATE
        -Wall -Wextra -Wpedantic -Werror=return-type -Werror=float-conversion -Wshadow
    )
    target_link_libraries(test_albert_native PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME TestAlbertNativeCore COMMAND test_albert_native)
    set_tests_properties(TestAlbertNativeCore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
