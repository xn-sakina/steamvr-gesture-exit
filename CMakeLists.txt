cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BAILOUT_VERSION)
string(STRIP "${BAILOUT_VERSION}" BAILOUT_VERSION)
if(NOT BAILOUT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic X.Y.Z version")
endif()

project(steamvr_bail_out VERSION "${BAILOUT_VERSION}" LANGUAGES CXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.json" BAILOUT_PACKAGE_JSON)
string(JSON BAILOUT_PACKAGE_VERSION GET "${BAILOUT_PACKAGE_JSON}" version)
if(NOT "${BAILOUT_PACKAGE_VERSION}" STREQUAL "${PROJECT_VERSION}")
    message(FATAL_ERROR "package.json version ${BAILOUT_PACKAGE_VERSION} must match CMake project version ${PROJECT_VERSION}")
endif()

option(BAILOUT_BUILD_TESTS "Build portable core tests" ON)
option(BAILOUT_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

find_program(BAILOUT_CLANG_FORMAT NAMES clang-format clang-format-20 clang-format-19 clang-format-18 clang-format-17)
if(BAILOUT_CLANG_FORMAT)
    file(GLOB_RECURSE BAILOUT_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/*.h"
    )
    add_custom_target(format
        COMMAND "${BAILOUT_CLANG_FORMAT}" -i ${BAILOUT_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${BAILOUT_CLANG_FORMAT}" --dry-run --Werror ${BAILOUT_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; format targets are unavailable (set BAILOUT_CLANG_FORMAT)")
endif()

add_library(bailout_jsoncpp STATIC openvr/src/jsoncpp.cpp)
target_include_directories(bailout_jsoncpp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/openvr/src")

add_library(bailout_core STATIC
    src/core/types.cpp
    src/core/gesture_detector.cpp
    src/core/settings.cpp
    src/core/settings_persistence.cpp
    src/core/state_machine.cpp
    src/core/diagnostics.cpp
    src/core/localization.cpp
    src/core/vr_game_classifier.cpp
    src/core/dashboard_ui.cpp
    src/core/wrist_pose_smoother.cpp
)
target_include_directories(bailout_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(bailout_core PUBLIC cxx_std_20)
target_link_libraries(bailout_core PRIVATE bailout_jsoncpp)

if(MSVC)
    target_compile_options(bailout_core PRIVATE /W4 /permissive- /utf-8)
    if(BAILOUT_WARNINGS_AS_ERRORS)
        target_compile_options(bailout_core PRIVATE /WX)
    endif()
else()
    target_compile_options(bailout_core PRIVATE -Wall -Wextra -Wpedantic)
    if(BAILOUT_WARNINGS_AS_ERRORS)
        target_compile_options(bailout_core PRIVATE -Werror)
    endif()
endif()

if(WIN32)
    enable_language(RC)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
        @ONLY
    )
    set(OPENVR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/openvr")
    if(NOT EXISTS "${OPENVR_ROOT}/headers/openvr.h")
        message(FATAL_ERROR "The bundled OpenVR SDK is required at ${OPENVR_ROOT}")
    endif()

    add_library(openvr_api SHARED IMPORTED GLOBAL)
    set_target_properties(openvr_api PROPERTIES
        IMPORTED_IMPLIB "${OPENVR_ROOT}/lib/win64/openvr_api.lib"
        IMPORTED_LOCATION "${OPENVR_ROOT}/bin/win64/openvr_api.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENVR_ROOT}/headers"
    )

    set_source_files_properties(assets/brand/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/bailout.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/bailout.png")

    add_executable(steamvr_bail_out WIN32
        src/app/main.cpp
        src/app/runtime_app.cpp
        src/platform/openvr/openvr_runtime.cpp
        src/platform/openvr/overlay_manager.cpp
        src/platform/openvr/d3d_overlay_renderer.cpp
        src/platform/windows/exit_target.cpp
        src/platform/windows/process_controller.cpp
        src/platform/windows/paths.cpp
        src/platform/windows/countdown_sound.cpp
        assets/brand/app.rc
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    )
    target_include_directories(steamvr_bail_out PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand")
    target_compile_features(steamvr_bail_out PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_bail_out PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
    if(MSVC)
        target_compile_options(steamvr_bail_out PRIVATE /W4 /permissive- /utf-8)
        if(BAILOUT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_bail_out PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_bail_out PRIVATE -Wall -Wextra -Wpedantic)
        if(BAILOUT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_bail_out PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        # Keep macOS/Linux cross-built Windows packages self-contained instead
        # of requiring MinGW runtime DLLs on the tester's machine.
        target_link_options(steamvr_bail_out PRIVATE -municode -static)
    endif()
    target_link_libraries(steamvr_bail_out PRIVATE
        bailout_core
        openvr_api
        d3d11
        dxgi
        d2d1
        dwrite
        windowscodecs
        shell32
        ole32
        user32
        advapi32
        winmm
    )

    add_executable(steamvr_bail_out_cli
        src/app/cli_main.cpp
        src/platform/openvr/openvr_runtime.cpp
        src/platform/windows/paths.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    )
    target_include_directories(steamvr_bail_out_cli PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${OPENVR_ROOT}/headers"
    )
    target_compile_features(steamvr_bail_out_cli PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_bail_out_cli PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        BAILOUT_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(steamvr_bail_out_cli PRIVATE /W4 /permissive- /utf-8)
        if(BAILOUT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_bail_out_cli PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_bail_out_cli PRIVATE -Wall -Wextra -Wpedantic)
        if(BAILOUT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_bail_out_cli PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        target_link_options(steamvr_bail_out_cli PRIVATE -municode -static)
    endif()
    target_link_libraries(steamvr_bail_out_cli PRIVATE openvr_api shell32 ole32)

    add_custom_command(TARGET steamvr_bail_out POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${OPENVR_ROOT}/bin/win64/openvr_api.dll"
            "$<TARGET_FILE_DIR:steamvr_bail_out>/openvr_api.dll"
        VERBATIM
    )

    install(TARGETS steamvr_bail_out RUNTIME DESTINATION bin/win64)
    install(TARGETS steamvr_bail_out_cli RUNTIME DESTINATION bin/win64 CONFIGURATIONS Debug)
    install(FILES "${OPENVR_ROOT}/bin/win64/openvr_api.dll" DESTINATION bin/win64)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/manifest/steamvr_bail_out.vrmanifest"
        DESTINATION manifest)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/bailout.png"
        DESTINATION assets/brand)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
        DESTINATION docs
        CONFIGURATIONS Debug
        FILES_MATCHING PATTERN "*.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION . CONFIGURATIONS Debug)
endif()

if(BAILOUT_BUILD_TESTS)
    include(CTest)
    add_executable(bailout_core_tests tests/core_tests.cpp)
    target_link_libraries(bailout_core_tests PRIVATE bailout_core)
    target_compile_features(bailout_core_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(bailout_core_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(bailout_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME bailout_core_tests COMMAND bailout_core_tests)
endif()
