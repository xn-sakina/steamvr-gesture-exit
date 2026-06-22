cmake_minimum_required(VERSION 3.20)

project(steamvr_bail_out VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SBO_BUILD_RUNTIME "Build SteamVR runtime executable" ON)
set(OPENVR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/openvr" CACHE PATH "Path to the checked-in OpenVR SDK")
set(SBO_GENERATED_STEAMVR_DIR "${CMAKE_BINARY_DIR}/generated/steamvr")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/steamvr/steamvr-bail-out.vrmanifest.in"
  "${SBO_GENERATED_STEAMVR_DIR}/steamvr-bail-out.vrmanifest"
  COPYONLY
)

add_library(steamvr_bail_out_core
  src/diagnostics.cpp
  src/flow.cpp
  src/gesture.cpp
)
target_include_directories(steamvr_bail_out_core PUBLIC src)

if(MSVC)
  target_compile_options(steamvr_bail_out_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(steamvr_bail_out_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(steamvr_bail_out_platform
  src/windows_platform.cpp
)
target_include_directories(steamvr_bail_out_platform PUBLIC src)
target_link_libraries(steamvr_bail_out_platform PUBLIC steamvr_bail_out_core)
if(WIN32)
  target_compile_definitions(steamvr_bail_out_platform PUBLIC SBO_WINDOWS_RUNTIME=1)
  target_link_libraries(steamvr_bail_out_platform PUBLIC user32 shell32)
endif()

if(SBO_BUILD_RUNTIME)
  add_executable(steamvr-bail-out
    src/main.cpp
    src/runtime.cpp
  )
  target_include_directories(steamvr-bail-out PRIVATE "${OPENVR_ROOT}/headers")
  target_link_libraries(steamvr-bail-out PRIVATE steamvr_bail_out_core steamvr_bail_out_platform)

  if(WIN32)
    target_compile_definitions(steamvr-bail-out PRIVATE SBO_WITH_OPENVR=1)
    target_link_libraries(steamvr-bail-out PRIVATE "${OPENVR_ROOT}/lib/win64/openvr_api.lib" winmm gdi32)
    add_custom_command(TARGET steamvr-bail-out POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENVR_ROOT}/bin/win64/openvr_api.dll"
        "$<TARGET_FILE_DIR:steamvr-bail-out>/openvr_api.dll")
  endif()

  if(MSVC)
    target_compile_options(steamvr-bail-out PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(steamvr-bail-out PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

add_executable(steamvr_bail_out_tests
  tests/state_machine_tests.cpp
  tests/gesture_tests.cpp
  tests/settings_tests.cpp
)
target_link_libraries(steamvr_bail_out_tests PRIVATE steamvr_bail_out_core steamvr_bail_out_platform)

enable_testing()
add_test(NAME steamvr_bail_out_tests COMMAND steamvr_bail_out_tests)

if(WIN32 AND SBO_BUILD_RUNTIME)
  add_custom_target(package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package/bin/win64"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/steamvr" "${CMAKE_BINARY_DIR}/package"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SBO_GENERATED_STEAMVR_DIR}/steamvr-bail-out.vrmanifest" "${CMAKE_BINARY_DIR}/package/steamvr-bail-out.vrmanifest"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:steamvr-bail-out>" "${CMAKE_BINARY_DIR}/package/bin/win64/steamvr-bail-out.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENVR_ROOT}/bin/win64/openvr_api.dll" "${CMAKE_BINARY_DIR}/package/bin/win64/openvr_api.dll"
    DEPENDS steamvr-bail-out
  )
else()
  add_custom_target(package
    COMMAND ${CMAKE_COMMAND} -E echo "The SteamVR runtime package must be built on Windows or with a Windows cross-toolchain."
    COMMAND ${CMAKE_COMMAND} -E false
  )
endif()
