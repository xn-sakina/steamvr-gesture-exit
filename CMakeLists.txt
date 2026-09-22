cmake_minimum_required(VERSION 3.24)

project(steamvr_bail_out VERSION 0.1.0 LANGUAGES CXX)

option(BAILOUT_BUILD_TESTS "Build portable core tests" ON)
option(BAILOUT_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

add_library(bailout_core STATIC
    src/core/types.cpp
    src/core/gesture_detector.cpp
    src/core/settings.cpp
    src/core/state_machine.cpp
    src/core/diagnostics.cpp
    src/core/localization.cpp
    src/core/vr_game_classifier.cpp
    src/core/dashboard_ui.cpp
)
target_include_directories(bailout_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(bailout_core PUBLIC cxx_std_20)

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

    add_executable(steamvr_bail_out
        src/app/main.cpp
        src/app/runtime_app.cpp
        src/platform/openvr/openvr_runtime.cpp
        src/platform/openvr/overlay_manager.cpp
        src/platform/openvr/d3d_overlay_renderer.cpp
        src/platform/windows/exit_target.cpp
        src/platform/windows/process_controller.cpp
        src/platform/windows/paths.cpp
        src/platform/windows/countdown_sound.cpp
    )
    target_compile_features(steamvr_bail_out PRIVATE cxx_std_20)
    target_compile_definitions(steamvr_bail_out PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        BAILOUT_VERSION="${PROJECT_VERSION}"
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

    add_custom_command(TARGET steamvr_bail_out POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${OPENVR_ROOT}/bin/win64/openvr_api.dll"
            "$<TARGET_FILE_DIR:steamvr_bail_out>/openvr_api.dll"
        VERBATIM
    )

    install(TARGETS steamvr_bail_out RUNTIME DESTINATION bin/win64)
    install(FILES "${OPENVR_ROOT}/bin/win64/openvr_api.dll" DESTINATION bin/win64)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/manifest/steamvr_bail_out.vrmanifest"
        DESTINATION manifest)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
        DESTINATION docs
        FILES_MATCHING PATTERN "*.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION .)
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
