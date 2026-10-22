cmake_minimum_required(VERSION 3.20)
# Calendar version YYYY.MM.N: set only at a release; until then the next planned one.
project(chartgrabber_pi VERSION 2026.10.3 LANGUAGES CXX)

option(CG_BUILD_TESTS "Build the GoogleTest unit tests" ON)
# Host tuple of the build host's OpenCPN, e.g. debian-x86_64 (see the README). Never "all":
# this wx-linked library must be refused on every other host. Empty is allowed here (a test-only build);
# tools/tarball.sh refuses an empty one.
set(CG_TARGET "" CACHE STRING "metadata.xml <target> : the host tuple, never all")
string(TOLOWER "${CG_TARGET}" CG_TARGET_LOWER)
if(CG_TARGET_LOWER STREQUAL "all")
    message(FATAL_ERROR "CG_TARGET \"all\" is forbidden : the plugin links wxWidgets, whose ABI differs between "
        "distributions; use the host tuple of the"
        " build host's OpenCPN, e.g. debian-x86_64")
endif()
set(CG_TARGET_VERSION "13" CACHE STRING "metadata.xml <target-version>")
if(UNIX)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE CG_HOST_MACHINE OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(CG_HOST_MACHINE STREQUAL "aarch64")
        set(CG_HOST_MACHINE "arm64")
    endif()
elseif(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CG_HOST_MACHINE "x86") # Windows plugin DLL: x86
endif()
set(CG_TARGET_ARCH "${CG_HOST_MACHINE}" CACHE STRING "metadata.xml <target-arch>: x86_64 or arm64")
set(CG_AUTHOR "Chart Grabber contributors" CACHE STRING "metadata.xml <author>")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Sanitizer builds (cmake -DCG_SANITIZE=address): applies to every target, own code and tests.
set(CG_SANITIZE "" CACHE STRING "Sanitizer build: empty or address (+undefined)")
if(MSVC AND NOT CG_SANITIZE STREQUAL "")
    message(FATAL_ERROR "CG_SANITIZE is not supported with MSVC")
endif()
if(CG_SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(NOT CG_SANITIZE STREQUAL "")
    message(FATAL_ERROR "CG_SANITIZE must be empty or address")
endif()

find_program(CG_CCACHE ccache)
if(CG_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CG_CCACHE}")
endif()

# Flags for our own code only (never for api/ or wx headers: those are SYSTEM includes).
add_library(cg_options INTERFACE)
if(MSVC)
    target_compile_options(cg_options INTERFACE /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /EHsc)
    # The first must reach every object of the DLL: OpenCPN 5.14 builds with it and ships msvcp140.dll 14.12.
    target_compile_definitions(cg_options INTERFACE _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(cg_options INTERFACE -Wall -Wextra -Wpedantic -Werror
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()

find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base xml)
find_package(OpenGL REQUIRED)
if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0) # progress_strip.cpp: gtk_window_begin_move_drag
endif()

add_subdirectory(src/core)

# The module. Append ONE source file per line at the END of the list.
set(CG_WX_SOURCES
    src/wx/chartgrabber_pi.cpp
    src/wx/area_tool.cpp
    src/wx/config_store.cpp
    src/wx/look.cpp
    src/wx/settings_dialog.cpp
    src/wx/cli_runner.cpp
    src/wx/messages.cpp
    src/wx/progress_strip.cpp
    src/wx/job_controller.cpp
    src/wx/main_dialog.cpp
    src/wx/new_chart_panel.cpp
    src/wx/my_charts_panel.cpp
    src/wx/sources_dialog.cpp
    src/wx/question.cpp
    src/wx/plugin_log.cpp
)
add_library(chartgrabber_pi SHARED ${CG_WX_SOURCES})
target_include_directories(chartgrabber_pi SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/api ${wxWidgets_INCLUDE_DIRS})
target_include_directories(chartgrabber_pi PRIVATE ${CMAKE_SOURCE_DIR}/src/wx)
target_compile_definitions(chartgrabber_pi PRIVATE ${wxWidgets_DEFINITIONS}
    CG_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} CG_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    CG_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    "CG_VERSION_STRING=\"${PROJECT_VERSION}\"")
target_link_libraries(chartgrabber_pi PRIVATE cg_core cg_options ${wxWidgets_LIBRARIES} OpenGL::GL)
if(UNIX)
    target_link_libraries(chartgrabber_pi PRIVATE PkgConfig::GTK3)
endif()
# Windows: the API symbols resolve against opencpn.lib (opencpn-libs api-18/msvc-wx32).
set(CG_OPENCPN_LIB "" CACHE FILEPATH "opencpn.lib of opencpn-libs api-18/msvc-wx32 (Windows only)")
if(WIN32)
    if(NOT CG_OPENCPN_LIB)
        message(FATAL_ERROR "CG_OPENCPN_LIB must name opencpn.lib of opencpn-libs api-18/msvc-wx32")
    endif()
    target_link_libraries(chartgrabber_pi PRIVATE "${CG_OPENCPN_LIB}")
endif()
# API symbols resolve against the opencpn executable at load time: no -Wl,--no-undefined.

# metadata.xml for the tarball; tools/tarball.sh packs it with the files below.
configure_file(packaging/metadata.xml.in "${CMAKE_BINARY_DIR}/metadata.xml" @ONLY)
# The version of a Debian OpenCPN can be "n/a": no slash in a file name.
# File name and top dir use the human release number: "n/a" -> major VERSION_ID of the build system (Debian 13: 13);
# metadata.xml keeps n/a (it must equal the OpenCPN host tuple). Without os-release the file name keeps "n-a".
set(CG_TARGET_VERSION_FILE "${CG_TARGET_VERSION}")
if(CG_TARGET_VERSION STREQUAL "n/a" AND EXISTS /etc/os-release)
    file(STRINGS /etc/os-release CG_OSREL_ID REGEX "^VERSION_ID=")
    if(CG_OSREL_ID MATCHES "^VERSION_ID=\"?([0-9]+)")
        set(CG_TARGET_VERSION_FILE "${CMAKE_MATCH_1}")
    endif()
endif()
string(REPLACE "/" "-" CG_TARGET_VERSION_FILE "${CG_TARGET_VERSION_FILE}")
set(CG_TARBALL_NAME
    "chartgrabber_pi-${PROJECT_VERSION}-${CG_TARGET}-${CG_TARGET_VERSION_FILE}-${CG_TARGET_ARCH}")
file(WRITE "${CMAKE_BINARY_DIR}/tarball-name.txt" "${CG_TARBALL_NAME}\n")

if(CG_BUILD_TESTS AND UNIX)
    find_package(GTest REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()
