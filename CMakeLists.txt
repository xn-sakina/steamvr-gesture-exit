cmake_minimum_required(VERSION 3.24)

# Release packages must run on a clean supported Windows installation without
# requiring a separately deployed Visual C++ Redistributable. Apply the static
# MSVC runtime to every target created below; Debug keeps the matching debug CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" STEAMVR_GESTURE_EXIT_VERSION)
string(STRIP "${STEAMVR_GESTURE_EXIT_VERSION}" STEAMVR_GESTURE_EXIT_VERSION)
if(NOT STEAMVR_GESTURE_EXIT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic X.Y.Z version")
endif()

project(steamvr_gesture_exit VERSION "${STEAMVR_GESTURE_EXIT_VERSION}" LANGUAGES CXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/supported_vr_games.json"
)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.json" STEAMVR_GESTURE_EXIT_PACKAGE_JSON)
string(JSON STEAMVR_GESTURE_EXIT_PACKAGE_VERSION GET "${STEAMVR_GESTURE_EXIT_PACKAGE_JSON}" version)
if(NOT "${STEAMVR_GESTURE_EXIT_PACKAGE_VERSION}" STREQUAL "${PROJECT_VERSION}")
    message(FATAL_ERROR "package.json version ${STEAMVR_GESTURE_EXIT_PACKAGE_VERSION} must match CMake project version ${PROJECT_VERSION}")
endif()

set(STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/config/supported_vr_games.json")
file(READ "${STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_PATH}"
    STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_JSON)
string(JSON STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_SCHEMA ERROR_VARIABLE supported_games_error
    GET "${STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_JSON}" schema_version)
if(supported_games_error OR NOT STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_SCHEMA EQUAL 2)
    message(FATAL_ERROR "supported_vr_games.json must have schema_version 2")
endif()
string(JSON STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_REVIEWED_AT ERROR_VARIABLE supported_games_error
    GET "${STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_JSON}" reviewed_at)
if(supported_games_error OR
   NOT STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_REVIEWED_AT MATCHES
       "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
    message(FATAL_ERROR "supported_vr_games.json must have a YYYY-MM-DD reviewed_at date")
endif()
string(JSON STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_COUNT ERROR_VARIABLE supported_games_error
    LENGTH "${STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_JSON}" games)
if(supported_games_error OR STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_COUNT LESS 1)
    message(FATAL_ERROR "supported_vr_games.json must contain at least one game")
endif()

set(STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_ENTRIES "")
set(STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_IDS "")
set(STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_KEYS "")
set(STEAMVR_GESTURE_EXIT_HAS_VRCHAT FALSE)
math(EXPR STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_LAST
    "${STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_COUNT} - 1")
foreach(index RANGE ${STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_LAST})
    foreach(field name steam_app_id app_key store_url vr_evidence social_evidence
                  embodied_presence_evidence embodied_presence_url popularity_evidence)
        string(JSON game_${field} ERROR_VARIABLE supported_games_error
            GET "${STEAMVR_GESTURE_EXIT_SUPPORTED_GAMES_JSON}" games ${index} ${field})
        if(supported_games_error OR "${game_${field}}" STREQUAL "")
            message(FATAL_ERROR "supported_vr_games.json game ${index} has an invalid ${field}")
        endif()
    endforeach()
    if(NOT game_steam_app_id MATCHES "^[0-9]+$")
        message(FATAL_ERROR "supported_vr_games.json game ${index} has an invalid steam_app_id")
    endif()
    set(expected_app_key "steam.app.${game_steam_app_id}")
    if(NOT game_app_key STREQUAL expected_app_key)
        message(FATAL_ERROR "supported_vr_games.json game ${index} app_key must be ${expected_app_key}")
    endif()
    if(NOT game_store_url MATCHES "^https://store\\.steampowered\\.com/app/${game_steam_app_id}(/|$)")
        message(FATAL_ERROR "supported_vr_games.json game ${index} must cite its official Steam page")
    endif()
    if(NOT game_embodied_presence_url MATCHES "^https://")
        message(FATAL_ERROR
            "supported_vr_games.json game ${index} must cite embodied-presence evidence")
    endif()
    if(game_steam_app_id IN_LIST STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_IDS)
        message(FATAL_ERROR "supported_vr_games.json contains duplicate Steam app id ${game_steam_app_id}")
    endif()
    if(game_app_key IN_LIST STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_KEYS)
        message(FATAL_ERROR "supported_vr_games.json contains duplicate app key ${game_app_key}")
    endif()
    list(APPEND STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_IDS "${game_steam_app_id}")
    list(APPEND STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_KEYS "${game_app_key}")
    string(APPEND STEAMVR_GESTURE_EXIT_SUPPORTED_GAME_ENTRIES
        "        \"${game_app_key}\",\n")
    if(game_app_key STREQUAL "steam.app.438100")
        set(STEAMVR_GESTURE_EXIT_HAS_VRCHAT TRUE)
    endif()
endforeach()
if(NOT STEAMVR_GESTURE_EXIT_HAS_VRCHAT)
    message(FATAL_ERROR "supported_vr_games.json must contain VRChat (steam.app.438100)")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/include/core")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/supported_vr_games.generated.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/core/supported_vr_games.generated.hpp"
    @ONLY
)

option(STEAMVR_GESTURE_EXIT_BUILD_TESTS "Build portable core tests" ON)
option(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

find_program(STEAMVR_GESTURE_EXIT_CLANG_FORMAT NAMES clang-format clang-format-20 clang-format-19 clang-format-18 clang-format-17)
if(NOT STEAMVR_GESTURE_EXIT_CLANG_FORMAT AND APPLE)
    execute_process(
        COMMAND xcrun --find clang-format
        OUTPUT_VARIABLE STEAMVR_GESTURE_EXIT_CLANG_FORMAT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(STEAMVR_GESTURE_EXIT_CLANG_FORMAT)
    file(GLOB_RECURSE STEAMVR_GESTURE_EXIT_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/*.h"
    )
    add_custom_target(format
        COMMAND "${STEAMVR_GESTURE_EXIT_CLANG_FORMAT}" -i ${STEAMVR_GESTURE_EXIT_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${STEAMVR_GESTURE_EXIT_CLANG_FORMAT}" --dry-run --Werror ${STEAMVR_GESTURE_EXIT_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; format targets are unavailable (set STEAMVR_GESTURE_EXIT_CLANG_FORMAT)")
endif()

add_library(steamvr_gesture_exit_jsoncpp STATIC openvr/src/jsoncpp.cpp)
target_include_directories(steamvr_gesture_exit_jsoncpp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/openvr/src")

add_library(steamvr_gesture_exit_appconfig STATIC
    src/platform/openvr/appconfig_editor.cpp
)
target_include_directories(steamvr_gesture_exit_appconfig PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(steamvr_gesture_exit_appconfig PUBLIC cxx_std_20)
target_link_libraries(steamvr_gesture_exit_appconfig PRIVATE steamvr_gesture_exit_jsoncpp)
if(WIN32)
    target_compile_definitions(steamvr_gesture_exit_appconfig PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(steamvr_gesture_exit_appconfig PRIVATE /W4 /permissive- /utf-8)
    if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
        target_compile_options(steamvr_gesture_exit_appconfig PRIVATE /WX)
    endif()
else()
    target_compile_options(steamvr_gesture_exit_appconfig PRIVATE -Wall -Wextra -Wpedantic)
    if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
        target_compile_options(steamvr_gesture_exit_appconfig PRIVATE -Werror)
    endif()
endif()

add_library(steamvr_gesture_exit_core STATIC
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
target_include_directories(steamvr_gesture_exit_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(steamvr_gesture_exit_core PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
target_compile_features(steamvr_gesture_exit_core PUBLIC cxx_std_20)
target_link_libraries(steamvr_gesture_exit_core PRIVATE steamvr_gesture_exit_jsoncpp)

if(MSVC)
    target_compile_options(steamvr_gesture_exit_core PRIVATE /W4 /permissive- /utf-8)
    if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
        target_compile_options(steamvr_gesture_exit_core PRIVATE /WX)
    endif()
else()
    target_compile_options(steamvr_gesture_exit_core PRIVATE -Wall -Wextra -Wpedantic)
    if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
        target_compile_options(steamvr_gesture_exit_core PRIVATE -Werror)
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
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/steamvr-gesture-exit.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/steamvr-gesture-exit.png")

    add_executable(steamvr_gesture_exit WIN32
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
    target_include_directories(steamvr_gesture_exit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand")
    target_compile_features(steamvr_gesture_exit PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_gesture_exit PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        STEAMVR_GESTURE_EXIT_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(steamvr_gesture_exit PRIVATE /W4 /permissive- /utf-8)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_gesture_exit PRIVATE -Wall -Wextra -Wpedantic)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        # Keep macOS/Linux cross-built Windows packages self-contained instead
        # of requiring MinGW runtime DLLs on the tester's machine.
        target_link_options(steamvr_gesture_exit PRIVATE -municode -static)
    endif()
    target_link_libraries(steamvr_gesture_exit PRIVATE
        steamvr_gesture_exit_core
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

    add_executable(steamvr_gesture_exit_cli
        src/app/cli_main.cpp
        src/platform/openvr/openvr_runtime.cpp
        src/platform/windows/paths.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    )
    target_include_directories(steamvr_gesture_exit_cli PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${OPENVR_ROOT}/headers"
    )
    target_compile_features(steamvr_gesture_exit_cli PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_gesture_exit_cli PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        STEAMVR_GESTURE_EXIT_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(steamvr_gesture_exit_cli PRIVATE /W4 /permissive- /utf-8)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_cli PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_gesture_exit_cli PRIVATE -Wall -Wextra -Wpedantic)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_cli PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        target_link_options(steamvr_gesture_exit_cli PRIVATE -municode -static)
    endif()
    target_link_libraries(steamvr_gesture_exit_cli PRIVATE openvr_api shell32 ole32)

    add_executable(steamvr_gesture_exit_appconfig_editor
        src/platform/windows/appconfig_editor_main.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    )
    target_include_directories(steamvr_gesture_exit_appconfig_editor PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_features(steamvr_gesture_exit_appconfig_editor PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_gesture_exit_appconfig_editor PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        STEAMVR_GESTURE_EXIT_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(steamvr_gesture_exit_appconfig_editor PRIVATE /W4 /permissive- /utf-8)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_appconfig_editor PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_gesture_exit_appconfig_editor PRIVATE -Wall -Wextra -Wpedantic)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_appconfig_editor PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        target_link_options(steamvr_gesture_exit_appconfig_editor PRIVATE -municode -static)
    endif()
    target_link_libraries(steamvr_gesture_exit_appconfig_editor PRIVATE
        steamvr_gesture_exit_appconfig
    )

    add_executable(steamvr_gesture_exit_coordinator_runner
        src/platform/windows/coordinator_runner.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc"
    )
    target_compile_features(steamvr_gesture_exit_coordinator_runner PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_gesture_exit_coordinator_runner PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        STEAMVR_GESTURE_EXIT_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_options(steamvr_gesture_exit_coordinator_runner PRIVATE /W4 /permissive- /utf-8)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_coordinator_runner PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_gesture_exit_coordinator_runner PRIVATE -Wall -Wextra -Wpedantic)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_coordinator_runner PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        target_link_options(steamvr_gesture_exit_coordinator_runner PRIVATE -municode -static)
    endif()
    target_link_libraries(steamvr_gesture_exit_coordinator_runner PRIVATE shell32)

    add_custom_command(TARGET steamvr_gesture_exit POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${OPENVR_ROOT}/bin/win64/openvr_api.dll"
            "$<TARGET_FILE_DIR:steamvr_gesture_exit>/openvr_api.dll"
        VERBATIM
    )

    install(TARGETS steamvr_gesture_exit RUNTIME DESTINATION bin/win64)
    install(TARGETS steamvr_gesture_exit_cli RUNTIME DESTINATION bin/win64 CONFIGURATIONS Debug)
    install(TARGETS steamvr_gesture_exit_appconfig_editor RUNTIME DESTINATION bin/win64 CONFIGURATIONS Debug)
    install(FILES "${OPENVR_ROOT}/bin/win64/openvr_api.dll" DESTINATION bin/win64)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/manifest/steamvr_gesture_exit.vrmanifest"
        DESTINATION manifest)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/steamvr-gesture-exit.png"
        DESTINATION assets/brand)
endif()

if(STEAMVR_GESTURE_EXIT_BUILD_TESTS)
    include(CTest)
    add_executable(steamvr_gesture_exit_core_tests tests/core_tests.cpp)
    target_link_libraries(steamvr_gesture_exit_core_tests PRIVATE steamvr_gesture_exit_core)
    target_compile_features(steamvr_gesture_exit_core_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(steamvr_gesture_exit_core_tests PRIVATE /W4 /permissive- /utf-8)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_core_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(steamvr_gesture_exit_core_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(STEAMVR_GESTURE_EXIT_WARNINGS_AS_ERRORS)
            target_compile_options(steamvr_gesture_exit_core_tests PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME steamvr_gesture_exit_core_tests COMMAND steamvr_gesture_exit_core_tests)
endif()
