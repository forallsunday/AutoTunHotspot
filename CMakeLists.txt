cmake_minimum_required(VERSION 3.21)
project(AutoTunHotspot VERSION 1.1.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "AutoTunHotspot requires Windows and MSVC (C++/WinRT).")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(TARGET_PLATFORM "windows-x64" CACHE STRING "Platform directory under bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/${TARGET_PLATFORM}/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/${TARGET_PLATFORM}/$<CONFIG>")
find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Concurrent Network Svg)

add_library(hotspot_backend STATIC src/network.cpp src/network.h src/policy.h src/powershell.cpp src/powershell.h)
target_compile_definitions(hotspot_backend PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(hotspot_backend PUBLIC /utf-8 /W4 /permissive- /EHsc)
target_link_libraries(hotspot_backend PUBLIC Qt5::Core windowsapp iphlpapi ws2_32 ole32 advapi32)
target_include_directories(hotspot_backend PUBLIC src)

add_executable(icon_generator tools/icon_generator.cpp src/icons.cpp assets/resources.qrc)
target_include_directories(icon_generator PRIVATE src)
target_link_libraries(icon_generator PRIVATE Qt5::Svg Qt5::Gui)
target_compile_options(icon_generator PRIVATE /utf-8)
set(APP_ICON_PATH "${CMAKE_BINARY_DIR}/generated/hotspot.ico")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${APP_ICON_PATH}"
    COMMAND icon_generator "${APP_ICON_PATH}"
    DEPENDS icon_generator assets/wifi-tethering.svg VERBATIM)
add_custom_target(app_icon DEPENDS "${APP_ICON_PATH}")
configure_file(assets/app.rc.in "${CMAKE_BINARY_DIR}/generated/app.rc" @ONLY)
set_source_files_properties("${CMAKE_BINARY_DIR}/generated/app.rc" PROPERTIES OBJECT_DEPENDS "${APP_ICON_PATH}")

add_executable(AutoTunHotspot WIN32
    src/main.cpp src/mainwindow.cpp src/mainwindow.h src/monitor.cpp src/monitor.h
    src/toggleswitch.cpp src/toggleswitch.h src/icons.cpp assets/resources.qrc "${CMAKE_BINARY_DIR}/generated/app.rc")
add_dependencies(AutoTunHotspot app_icon)
target_link_libraries(AutoTunHotspot PRIVATE hotspot_backend Qt5::Widgets Qt5::Concurrent Qt5::Network Qt5::Svg shell32)
target_compile_definitions(AutoTunHotspot PRIVATE AUTOTUNHOTSPOT_VERSION="${PROJECT_VERSION}")
target_link_options(AutoTunHotspot PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")

# vcpkg copies DLL dependencies beside the executable. Copy the Qt platform plugin, too.
get_target_property(QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
get_filename_component(QT_TOOLS_BIN "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
get_filename_component(QT_INSTALL_ROOT "${Qt5_DIR}/../../.." ABSOLUTE)
if(EXISTS "${QT_INSTALL_ROOT}/plugins/platforms/qwindows.dll")
    add_custom_command(TARGET AutoTunHotspot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AutoTunHotspot>/plugins/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_INSTALL_ROOT}/$<IF:$<CONFIG:Debug>,debug/,>plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:AutoTunHotspot>/plugins/platforms/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/qt.conf"
            "$<TARGET_FILE_DIR:AutoTunHotspot>/qt.conf")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(policy_tests tests/policy_tests.cpp)
    set_target_properties(policy_tests PROPERTIES AUTOMOC OFF)
    target_include_directories(policy_tests PRIVATE src)
    target_compile_options(policy_tests PRIVATE /utf-8 /W4)
    add_test(NAME recovery_policy COMMAND policy_tests)
    add_executable(powershell_tests tests/powershell_tests.cpp)
    set_target_properties(powershell_tests PROPERTIES AUTOMOC OFF)
    target_link_libraries(powershell_tests PRIVATE hotspot_backend)
    add_test(NAME powershell_adapter_recovery COMMAND powershell_tests)
    set_tests_properties(powershell_adapter_recovery PROPERTIES TIMEOUT 45)
    add_test(NAME ui_smoke COMMAND AutoTunHotspot --smoke-test "${CMAKE_BINARY_DIR}/ui-smoke.json")
    set_tests_properties(ui_smoke PROPERTIES TIMEOUT 30)
endif()

install(TARGETS AutoTunHotspot RUNTIME DESTINATION .)
install(FILES README.md CHANGELOG.md qt.conf DESTINATION .)
install(FILES assets/NOTICE.md assets/LICENSE-material-icons.txt DESTINATION licenses)
foreach(QT_PORT qt5-base qt5-svg qt5-imageformats brotli bzip2 double-conversion freetype harfbuzz
        libjpeg-turbo liblzma libpng libwebp openssl pcre2 tiff zlib zstd)
    if(EXISTS "${QT_INSTALL_ROOT}/share/${QT_PORT}/copyright")
        install(FILES "${QT_INSTALL_ROOT}/share/${QT_PORT}/copyright" DESTINATION licenses RENAME "${QT_PORT}-copyright.txt")
    endif()
endforeach()
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)
install(DIRECTORY "$<TARGET_FILE_DIR:AutoTunHotspot>/" DESTINATION .
    FILES_MATCHING PATTERN "*.dll" PATTERN "*.conf")
