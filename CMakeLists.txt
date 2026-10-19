cmake_minimum_required(VERSION 3.16)

project(encryptEd VERSION 1.2 LANGUAGES C CXX)

# Prefer -O2 over CMake's default -O3 for GCC/Clang Release builds, including
# libraries built from source. Keep the other Release flags (e.g. -DNDEBUG).
foreach(language C CXX)
    if(CMAKE_${language}_COMPILER_ID MATCHES "GNU|Clang")
        string(REPLACE "-O3" "-O2" release_flags "${CMAKE_${language}_FLAGS_RELEASE}")
        set(CMAKE_${language}_FLAGS_RELEASE "${release_flags}" CACHE STRING
            "Flags used by the ${language} compiler during Release builds" FORCE)
    endif()
endforeach()
unset(release_flags)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
    add_link_options(-B/usr/bin -fuse-ld=bfd)
endif()

set(ENCRYPTED_ICON_SVG "${CMAKE_CURRENT_SOURCE_DIR}/docs/icon.svg")
set(ENCRYPTED_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/encryptEd.ico")
set(ENCRYPTED_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/encryptEd.icns")
set(ENCRYPTED_LINUX_ICON_SIZES 16 24 32 48 64 128 256 512)
set(ENCRYPTED_APP_AUTHOR "Tony Mattsson")
set(ENCRYPTED_APP_VERSION "${PROJECT_VERSION}")
set(ENCRYPTED_INSTALL_COMPONENT_GUI "gui_runtime")
set(ENCRYPTED_INSTALL_COMPONENT_CLI "cli")
set(ENCRYPTED_WINDOWS_DISPLAY_NAME "EncryptEd")
set(ENCRYPTED_WINDOWS_INSTALL_SUBDIR "EncryptEd")
set(ENCRYPTED_WINDOWS_PACKAGE_FILE_NAME "encryptEd-${ENCRYPTED_APP_VERSION}-windows")
set(ENCRYPTED_WINDOWS_INSTALL_REGISTRY_KEY "Software\\encryptEd")
set(ENCRYPTED_WINDOWS_UNINSTALL_REGISTRY_KEY
    "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EncryptEd"
)

function(encrypted_make_windows_self_contained target)
    if(WIN32 AND MINGW)
        target_link_options(${target} PRIVATE -static -static-libgcc ${ARGN})
    endif()
endfunction()

function(encrypted_apply_app_metadata target)
    target_compile_definitions(${target} PRIVATE
        ENCRYPTED_APP_AUTHOR="${ENCRYPTED_APP_AUTHOR}"
        ENCRYPTED_APP_VERSION="${ENCRYPTED_APP_VERSION}"
        ENCRYPTED_LUA_VERSION="${ENCRYPTED_LUA_VERSION}"
        ENCRYPTED_MONOCYPHER_VERSION="${ENCRYPTED_MONOCYPHER_VERSION}"
    )
endfunction()

option(ENCRYPTED_ENABLE_HARDENING
    "Enable address and undefined-behaviour sanitizers in Debug builds (requires -DCMAKE_BUILD_TYPE=Debug)"
    OFF)

function(encrypted_apply_hardening target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wformat=2)
        if(ENCRYPTED_ENABLE_HARDENING)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

function(encrypted_require_vendored_path path label instructions)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "${label} not found at:\n"
            "    ${path}\n"
            "${instructions}"
        )
    endif()
endfunction()

add_library(microz STATIC
    lib/microz/microz.c
)
target_include_directories(microz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/microz
)

encrypted_require_vendored_path(
    "${CMAKE_CURRENT_SOURCE_DIR}/external/monocypher/monocypher.c"
    "Monocypher"
    "  Download Monocypher 4.0.3 from https://monocypher.org/download/ and copy\n    monocypher.c and monocypher.h into external/monocypher/."
)

add_library(monocypher STATIC
    external/monocypher/monocypher.c
)
target_include_directories(monocypher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/monocypher
)

add_library(encrypted_core STATIC
    src/core/txx_buffer.c
    src/core/txx_codec.c
    src/core/txx_compress.c
    src/core/txx_crypto.c
    src/core/txx_format.c
    src/core/txx_status.c
    src/core/utf8.c
    src/platform/file_io.c
    src/platform/random.c
    src/platform/secure_mem.c
)
target_include_directories(encrypted_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(encrypted_core PUBLIC
    microz
    monocypher
)
if(WIN32)
    target_link_libraries(encrypted_core PUBLIC bcrypt)
endif()
encrypted_apply_hardening(encrypted_core)

# Text transformation utilities -- no FLTK dependency; unit-testable standalone
add_library(encrypted_text_transform STATIC
    src/gui/text_transform.cpp
)
target_include_directories(encrypted_text_transform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(encrypted_text_transform PUBLIC encrypted_core)
encrypted_apply_hardening(encrypted_text_transform)

# Restricted embedded Lua: omit the interpreter and all I/O/module libraries.
set(ENCRYPTED_LUA_SOURCES
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c
    ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lstrlib.c
    ltablib.c lmathlib.c lutf8lib.c
)
list(TRANSFORM ENCRYPTED_LUA_SOURCES PREPEND "lib/lua/src/")
add_library(encrypted_lua STATIC ${ENCRYPTED_LUA_SOURCES})
target_include_directories(encrypted_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/lua/src)
if(UNIX)
    target_link_libraries(encrypted_lua PUBLIC m)
endif()
encrypted_apply_hardening(encrypted_lua)
if(NOT MSVC)
    # Upstream string.format constructs and validates its format strings at runtime.
    target_compile_options(encrypted_lua PRIVATE -Wno-format-nonliteral)
endif()

# About dialogs report dependency versions from their source metadata.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/lua/src/lua.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/monocypher/monocypher.h")
file(STRINGS lib/lua/src/lua.h _lua_version_lines
    REGEX "^#define LUA_VERSION_(MAJOR|MINOR|RELEASE)_N[ \t]+[0-9]+")
foreach(_line IN LISTS _lua_version_lines)
    if(_line MATCHES "LUA_VERSION_(MAJOR|MINOR|RELEASE)_N[ \t]+([0-9]+)")
        set(_lua_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
endforeach()
set(ENCRYPTED_LUA_VERSION "${_lua_MAJOR}.${_lua_MINOR}.${_lua_RELEASE}")
file(STRINGS external/monocypher/monocypher.h _monocypher_version_line
    LIMIT_COUNT 1 REGEX "^// Monocypher version ")
string(REGEX REPLACE "^// Monocypher version " "" ENCRYPTED_MONOCYPHER_VERSION
    "${_monocypher_version_line}")

add_library(encrypted_plugins STATIC src/plugins/runtime.cpp src/plugins/catalog.cpp src/plugins/install.cpp)
target_include_directories(encrypted_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(encrypted_plugins PRIVATE encrypted_lua PUBLIC encrypted_core)
encrypted_apply_hardening(encrypted_plugins)
target_compile_definitions(encrypted_plugins PRIVATE
    ENCRYPTED_DEFAULT_PLUGIN_DIR="${CMAKE_INSTALL_FULL_DATAROOTDIR}/encryptEd/default-plugins"
)

add_executable(encrypted_cli
    src/tools/encrypted_cli.c
)
target_link_libraries(encrypted_cli PRIVATE encrypted_core)
encrypted_make_windows_self_contained(encrypted_cli)
encrypted_apply_hardening(encrypted_cli)
install(TARGETS encrypted_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_CLI}
)

# GUI executable — requires FLTK (detected via fltk-config)
set(ENCRYPTED_FLTK_VERSION "1_5" CACHE STRING
    "FLTK version directory (this project requires FLTK 1.5)")

if(WIN32 AND CMAKE_CROSSCOMPILING)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ENCRYPTED_FLTK_PREFIX
            "${CMAKE_CURRENT_SOURCE_DIR}/external/fltk/${ENCRYPTED_FLTK_VERSION}/fltk-win64")
    else()
        set(ENCRYPTED_FLTK_PREFIX
            "${CMAKE_CURRENT_SOURCE_DIR}/external/fltk/${ENCRYPTED_FLTK_VERSION}/fltk-win32")
    endif()

    if(EXISTS "${ENCRYPTED_FLTK_PREFIX}/CMake/FLTKConfig.cmake")
        find_package(FLTK 1.5 CONFIG REQUIRED
            PATHS
                "${ENCRYPTED_FLTK_PREFIX}"
                "${ENCRYPTED_FLTK_PREFIX}/CMake"
            NO_DEFAULT_PATH
        )

        set(ENCRYPTED_GUI_SOURCES
            src/gui/main_fltk.cpp
            src/gui/md_highlight.cpp
        )
        file(TO_NATIVE_PATH "${ENCRYPTED_ICON_ICO}" ENCRYPTED_ICON_ICO_RC)
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/encryptEd.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/encryptEd.rc
            @ONLY
        )
        list(APPEND ENCRYPTED_GUI_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/encryptEd.rc)

        add_executable(encryptEd WIN32
            ${ENCRYPTED_GUI_SOURCES}
        )
        target_link_libraries(encryptEd PRIVATE
            encrypted_core
            encrypted_text_transform
            encrypted_plugins
            fltk::fltk
        )
        target_include_directories(encryptEd PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        encrypted_apply_app_metadata(encryptEd)
        encrypted_apply_hardening(encryptEd)
        encrypted_make_windows_self_contained(encryptEd -static-libstdc++)

        install(TARGETS encryptEd
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
        )
    else()
        message(STATUS
            "Windows cross-build detected but no vendored FLTK package was found — "
            "skipping encryptEd GUI target.\n"
            "  To build the GUI, obtain a static FLTK ${ENCRYPTED_FLTK_VERSION} build "
            "for this target and place it at:\n"
            "    ${ENCRYPTED_FLTK_PREFIX}/\n"
            "  containing CMake/FLTKConfig.cmake (build FLTK from "
            "https://www.fltk.org/software.php with CMAKE_INSTALL_PREFIX set there)."
        )
    endif()
else()
# Prefer vendored FLTK CMake config (FLTK 1.5+) if present; fall back to fltk-config.
if(APPLE)
    set(_fltk_platform fltk-macos)
else()
    set(_fltk_platform fltk-linux)
endif()
set(_fltk_vendored_prefix
    "${CMAKE_CURRENT_SOURCE_DIR}/external/fltk/${ENCRYPTED_FLTK_VERSION}/${_fltk_platform}")
if(EXISTS "${_fltk_vendored_prefix}/share/fltk/FLTKConfig.cmake")
    find_package(FLTK 1.5 CONFIG REQUIRED
        PATHS "${_fltk_vendored_prefix}"
        NO_DEFAULT_PATH
    )
endif()
unset(_fltk_vendored_prefix)
# Honor explicit installation prefixes (for example a macOS/Homebrew build).
if(NOT TARGET fltk::fltk AND CMAKE_PREFIX_PATH)
    find_package(FLTK 1.5 CONFIG QUIET)
endif()

if(NOT TARGET fltk::fltk)
    # No vendored CMake config — use fltk-config from PATH (system shared library).
    find_program(FLTK_CONFIG fltk-config)
    if(FLTK_CONFIG)
        execute_process(COMMAND ${FLTK_CONFIG} --version
            OUTPUT_VARIABLE _fltk_version OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_fltk_version VERSION_LESS "1.5")
            message(STATUS "FLTK ${_fltk_version} is too old; FLTK 1.5 is required")
            unset(FLTK_CONFIG CACHE)
            unset(FLTK_CONFIG)
        endif()
    endif()
    if(FLTK_CONFIG)
        execute_process(
            COMMAND ${FLTK_CONFIG} --cxxflags
            OUTPUT_VARIABLE _fltk_cxx
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${FLTK_CONFIG} --ldflags
            OUTPUT_VARIABLE _fltk_ld
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        separate_arguments(_fltk_cxx_list UNIX_COMMAND "${_fltk_cxx}")
        separate_arguments(_fltk_ld_list  UNIX_COMMAND "${_fltk_ld}")

        foreach(_flag_var IN ITEMS _fltk_cxx_list _fltk_ld_list)
            set(_filtered_flags)
            foreach(_flag IN LISTS ${_flag_var})
                if(_flag MATCHES "^-flto($|=.*)" OR _flag STREQUAL "-ffat-lto-objects")
                    continue()
                endif()
                list(APPEND _filtered_flags "${_flag}")
            endforeach()
            set(${_flag_var} ${_filtered_flags})
        endforeach()
    endif()
endif()

if(TARGET fltk::fltk OR FLTK_CONFIG)
    set(ENCRYPTED_GUI_SOURCES
        src/gui/main_fltk.cpp
        src/gui/md_highlight.cpp
    )
    if(WIN32)
        file(TO_NATIVE_PATH "${ENCRYPTED_ICON_ICO}" ENCRYPTED_ICON_ICO_RC)
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/encryptEd.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/encryptEd.rc
            @ONLY
        )
        list(APPEND ENCRYPTED_GUI_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/encryptEd.rc)
    endif()
    if(APPLE)
        list(APPEND ENCRYPTED_GUI_SOURCES ${ENCRYPTED_ICON_ICNS})
    endif()

    add_executable(encryptEd WIN32 MACOSX_BUNDLE
        ${ENCRYPTED_GUI_SOURCES}
    )
    if(TARGET fltk::fltk)
        # CMake target: library order is handled automatically (works with static libs too).
        target_link_libraries(encryptEd PRIVATE encrypted_core encrypted_text_transform encrypted_plugins fltk::fltk)
    else()
        # Raw fltk-config flags: target_link_options places them before object files,
        # which is fine for shared libraries (system install).
        target_compile_options(encryptEd PRIVATE ${_fltk_cxx_list})
        target_link_options(encryptEd   PRIVATE ${_fltk_ld_list})
        target_link_libraries(encryptEd PRIVATE encrypted_core encrypted_text_transform encrypted_plugins)
    endif()
    target_include_directories(encryptEd PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    encrypted_apply_app_metadata(encryptEd)
    encrypted_apply_hardening(encryptEd)
    encrypted_make_windows_self_contained(encryptEd -static-libstdc++)
    if(APPLE)
        set_source_files_properties(${ENCRYPTED_ICON_ICNS}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources
        )
        set_target_properties(encryptEd PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME EncryptEd
            MACOSX_BUNDLE_GUI_IDENTIFIER com.bzoink.encryptEd
            MACOSX_BUNDLE_BUNDLE_VERSION ${ENCRYPTED_APP_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${ENCRYPTED_APP_VERSION}
            MACOSX_BUNDLE_ICON_FILE encryptEd.icns
        )
    endif()

    install(TARGETS encryptEd
        BUNDLE DESTINATION .
        COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
    )

    if(UNIX AND NOT APPLE)
        install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/encryptEd.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
            COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
        )
        foreach(size IN LISTS ENCRYPTED_LINUX_ICON_SIZES)
            install(FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/png/encryptEd-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps
                COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
                RENAME encryptEd.png
            )
        endforeach()
        install(FILES
            ${ENCRYPTED_ICON_SVG}
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
            COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
            RENAME encryptEd.svg
        )
    endif()
else()
    message(STATUS
        "FLTK not found — skipping encryptEd GUI target.\n"
        "  To build the GUI, either install a system FLTK 1.5 providing "
        "fltk-config on PATH,\n"
        "  or build FLTK 1.5 from https://www.fltk.org/software.php as a "
        "static library and place it at:\n"
        "    ${CMAKE_CURRENT_SOURCE_DIR}/external/fltk/${ENCRYPTED_FLTK_VERSION}/fltk-linux/\n"
        "  containing share/fltk/FLTKConfig.cmake "
        "(build with CMAKE_INSTALL_PREFIX set there)."
    )
endif()
endif()

# ---------------------------------------------------------------------------
# etui — text-mode (terminal) front-end. Built on FTXUI (no curses).
#
# The FTXUI-free portion (document model, markdown classifier, config,
# clipboard) is a static library so its tests build without FTXUI, mirroring how
# encrypted_text_transform is testable without FLTK. The executable ships with
# the GUI installer (same install component).
# ---------------------------------------------------------------------------
add_library(etui_core STATIC
    src/tui/edit_buffer.cpp
    src/tui/md_parse.cpp
    src/tui/syn_parse.cpp
    src/tui/config.cpp
    src/tui/clipboard.cpp
)
target_include_directories(etui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(etui_core PUBLIC encrypted_core)
encrypted_apply_hardening(etui_core)

option(ENCRYPTED_BUILD_TUI "Build the etui terminal front-end (requires FTXUI)" ON)

if(ENCRYPTED_BUILD_TUI)
    find_package(ftxui QUIET)
    if(NOT ftxui_FOUND)
        include(FetchContent)
        set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ftxui/CMakeLists.txt")
            add_subdirectory(external/ftxui ${CMAKE_CURRENT_BINARY_DIR}/_deps/ftxui-build)
        else()
            FetchContent_Declare(ftxui
                SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/ftxui"
                GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
                GIT_TAG v7.0.3
            )
            FetchContent_MakeAvailable(ftxui)
        endif()
    endif()

    if(TARGET ftxui::component)
        set(ENCRYPTED_FTXUI_VERSION "Unknown (system package)")
        if(ftxui_FOUND AND ftxui_VERSION)
            set(ENCRYPTED_FTXUI_VERSION "${ftxui_VERSION}")
        elseif(NOT ftxui_FOUND)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/external/ftxui/CMakeLists.txt")
            file(STRINGS external/ftxui/CMakeLists.txt _ftxui_version_line
                LIMIT_COUNT 1 REGEX "^[ \t]+VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
            string(REGEX REPLACE "^[ \t]+VERSION " "" ENCRYPTED_FTXUI_VERSION "${_ftxui_version_line}")
        endif()
        add_executable(etui
            src/tui/main_tui.cpp
        )
        set_target_properties(etui PROPERTIES OUTPUT_NAME etui)
        target_include_directories(etui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(etui PRIVATE
            etui_core
            encrypted_text_transform
            encrypted_plugins
            ftxui::screen
            ftxui::dom
            ftxui::component
        )
        encrypted_apply_app_metadata(etui)
        target_compile_definitions(etui PRIVATE ENCRYPTED_FTXUI_VERSION="${ENCRYPTED_FTXUI_VERSION}")
        encrypted_apply_hardening(etui)
        encrypted_make_windows_self_contained(etui -static-libstdc++)

        # Ships in the same installer/component as the encryptEd GUI binary.
        install(TARGETS etui
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI}
        )
    else()
        message(STATUS "FTXUI not available — skipping etui terminal front-end")
    endif()
endif()

enable_testing()

if(TARGET encryptEd)
    add_executable(plugin_buffer_tests tests/plugin_buffer_tests.cpp)
    target_link_libraries(plugin_buffer_tests PRIVATE encrypted_plugins)
    if(TARGET fltk::fltk)
        target_link_libraries(plugin_buffer_tests PRIVATE fltk::fltk)
    else()
        target_compile_options(plugin_buffer_tests PRIVATE ${_fltk_cxx_list})
        target_link_options(plugin_buffer_tests PRIVATE ${_fltk_ld_list})
    endif()
    encrypted_make_windows_self_contained(plugin_buffer_tests -static-libstdc++)
    encrypted_apply_hardening(plugin_buffer_tests)
    add_test(NAME plugin_buffer_tests COMMAND plugin_buffer_tests)
endif()

add_executable(plugin_tests tests/plugin_tests.cpp)
target_link_libraries(plugin_tests PRIVATE encrypted_plugins)
target_compile_definitions(plugin_tests PRIVATE ENCRYPTED_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
encrypted_make_windows_self_contained(plugin_tests -static-libstdc++)
encrypted_apply_hardening(plugin_tests)
add_test(NAME plugin_tests COMMAND plugin_tests)

add_executable(default_plugin_tests tests/default_plugin_tests.cpp)
target_link_libraries(default_plugin_tests PRIVATE encrypted_plugins)
target_compile_definitions(default_plugin_tests PRIVATE ENCRYPTED_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
encrypted_make_windows_self_contained(default_plugin_tests -static-libstdc++)
encrypted_apply_hardening(default_plugin_tests)
add_test(NAME default_plugin_tests COMMAND default_plugin_tests)


# Keep development executables relocatable, including multi-config builds.
file(GLOB_RECURSE ENCRYPTED_DEFAULT_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/plugins/*.lua")
foreach(editor IN ITEMS encryptEd etui)
    if(TARGET ${editor})
        set_property(TARGET ${editor} APPEND PROPERTY LINK_DEPENDS ${ENCRYPTED_DEFAULT_PLUGIN_SOURCES})
        add_custom_command(TARGET ${editor} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/plugins"
                "$<TARGET_FILE_DIR:${editor}>/default-plugins"
            VERBATIM)
    endif()
endforeach()
if(TARGET encryptEd)
    if(APPLE)
        add_custom_command(TARGET encryptEd POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/plugins"
                "$<TARGET_BUNDLE_DIR:encryptEd>/Contents/Resources/default-plugins"
            VERBATIM)
    endif()
endif()

install(DIRECTORY examples/plugins/ DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/encryptEd/default-plugins
    COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI})
install(FILES docs/plugins.md DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/encryptEd
    COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI})
install(FILES lib/lua/LICENSE DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/encryptEd/licenses
    RENAME Lua.txt COMPONENT ${ENCRYPTED_INSTALL_COMPONENT_GUI})

add_executable(encrypted_core_tests
    tests/core_tests.c
)
target_link_libraries(encrypted_core_tests PRIVATE encrypted_core)
encrypted_make_windows_self_contained(encrypted_core_tests)
encrypted_apply_hardening(encrypted_core_tests)
add_test(NAME encrypted_core_tests COMMAND encrypted_core_tests)

add_test(NAME encrypted_cli_roundtrip
    COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:encrypted_cli>
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_roundtrip
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_roundtrip.cmake
)

add_executable(text_transform_tests
    tests/text_transform_tests.cpp
)
target_link_libraries(text_transform_tests PRIVATE encrypted_text_transform)
encrypted_make_windows_self_contained(text_transform_tests)
encrypted_apply_hardening(text_transform_tests)
add_test(NAME text_transform_tests COMMAND text_transform_tests)

add_executable(tui_edit_buffer_tests
    tests/tui/edit_buffer_tests.cpp
)
target_link_libraries(tui_edit_buffer_tests PRIVATE etui_core)
encrypted_make_windows_self_contained(tui_edit_buffer_tests)
encrypted_apply_hardening(tui_edit_buffer_tests)
add_test(NAME tui_edit_buffer_tests COMMAND tui_edit_buffer_tests)

add_executable(tui_plugin_tests tests/tui/plugin_tests.cpp)
target_link_libraries(tui_plugin_tests PRIVATE etui_core encrypted_plugins)
encrypted_make_windows_self_contained(tui_plugin_tests -static-libstdc++)
encrypted_apply_hardening(tui_plugin_tests)
add_test(NAME tui_plugin_tests COMMAND tui_plugin_tests)
if(TARGET etui AND UNIX AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tui_plugin_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tui/plugin_smoke.py
                $<TARGET_FILE:etui>)
        set_tests_properties(tui_plugin_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME tui_mouse_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tui/mouse_smoke.py
                $<TARGET_FILE:etui>)
        set_tests_properties(tui_mouse_smoke PROPERTIES TIMEOUT 120)
        add_test(NAME tui_search_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tui/search_smoke.py
                $<TARGET_FILE:etui>)
        set_tests_properties(tui_search_smoke PROPERTIES TIMEOUT 120)
    endif()
endif()

set(CPACK_PACKAGE_NAME "encryptEd")
set(CPACK_PACKAGE_VENDOR "${ENCRYPTED_APP_AUTHOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Encrypted UTF-8 text editor")
set(CPACK_PACKAGE_VERSION ${ENCRYPTED_APP_VERSION})
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
if(TARGET encryptEd)
    set(CPACK_PACKAGE_EXECUTABLES "encryptEd" "${ENCRYPTED_WINDOWS_DISPLAY_NAME}")
endif()
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "${ENCRYPTED_WINDOWS_PACKAGE_FILE_NAME}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${ENCRYPTED_WINDOWS_INSTALL_SUBDIR}")
    if(TARGET encryptEd)
        set(CPACK_COMPONENTS_ALL ${ENCRYPTED_INSTALL_COMPONENT_GUI})
    endif()
    set(CPACK_NSIS_DISPLAY_NAME "${ENCRYPTED_WINDOWS_DISPLAY_NAME}")
    set(CPACK_NSIS_PACKAGE_NAME "${ENCRYPTED_WINDOWS_DISPLAY_NAME} ${ENCRYPTED_APP_VERSION}")
    set(CPACK_NSIS_UNINSTALL_NAME "Uninstall ${ENCRYPTED_WINDOWS_DISPLAY_NAME}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MUI_ICON "${ENCRYPTED_ICON_ICO}")
    set(CPACK_NSIS_MUI_UNIICON "${ENCRYPTED_ICON_ICO}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\encryptEd.exe")

    if(TARGET encryptEd)
        set(ENCRYPTED_NSIS_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/windows-installer")
        set(ENCRYPTED_NSIS_STAGE_DIR "${ENCRYPTED_NSIS_WORK_DIR}/stage")
        set(ENCRYPTED_NSIS_SCRIPT "${ENCRYPTED_NSIS_WORK_DIR}/encryptEd-installer.nsi")
        set(ENCRYPTED_NSIS_OUTPUT
            "${ENCRYPTED_NSIS_WORK_DIR}/${ENCRYPTED_WINDOWS_PACKAGE_FILE_NAME}-setup.exe"
        )
        set(ENCRYPTED_NSIS_STAGE_GLOB "${ENCRYPTED_NSIS_STAGE_DIR}/*")

        file(TO_NATIVE_PATH "${ENCRYPTED_ICON_ICO}" ENCRYPTED_ICON_ICO_NATIVE)
        file(TO_NATIVE_PATH "${ENCRYPTED_NSIS_STAGE_GLOB}" ENCRYPTED_NSIS_STAGE_GLOB_NATIVE)
        file(TO_NATIVE_PATH "${ENCRYPTED_NSIS_OUTPUT}" ENCRYPTED_NSIS_OUTPUT_NATIVE)

        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(ENCRYPTED_NSIS_PROGRAMFILES_DIRECTIVE "!define MULTIUSER_USE_PROGRAMFILES64")
            set(ENCRYPTED_NSIS_SET_REG_VIEW "  SetRegView 64")
        else()
            set(ENCRYPTED_NSIS_PROGRAMFILES_DIRECTIVE "")
            set(ENCRYPTED_NSIS_SET_REG_VIEW "")
        endif()

        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/encryptEd-installer.nsi.in
            ${ENCRYPTED_NSIS_SCRIPT}
            @ONLY
        )

        find_program(ENCRYPTED_MAKENSIS_EXECUTABLE NAMES makensis makensis.exe)
        mark_as_advanced(ENCRYPTED_MAKENSIS_EXECUTABLE)

        if(ENCRYPTED_MAKENSIS_EXECUTABLE)
            set(ENCRYPTED_NSIS_INSTALL_CONFIG_ARGS)
            if(CMAKE_CONFIGURATION_TYPES)
                list(APPEND ENCRYPTED_NSIS_INSTALL_CONFIG_ARGS --config "$<CONFIG>")
            endif()

            set(ENCRYPTED_INSTALLER_TARGETS encryptEd)
            if(TARGET etui)
                list(APPEND ENCRYPTED_INSTALLER_TARGETS etui)
            endif()
            add_custom_target(package_windows_installer
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${ENCRYPTED_NSIS_STAGE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${ENCRYPTED_NSIS_WORK_DIR}"
                COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                    ${ENCRYPTED_NSIS_INSTALL_CONFIG_ARGS}
                    --prefix "${ENCRYPTED_NSIS_STAGE_DIR}"
                    --component ${ENCRYPTED_INSTALL_COMPONENT_GUI}
                COMMAND "${ENCRYPTED_MAKENSIS_EXECUTABLE}" -V2 "${ENCRYPTED_NSIS_SCRIPT}"
                BYPRODUCTS "${ENCRYPTED_NSIS_OUTPUT}"
                COMMAND_EXPAND_LISTS
                DEPENDS ${ENCRYPTED_INSTALLER_TARGETS}
                VERBATIM
                COMMENT "Staging ${ENCRYPTED_WINDOWS_DISPLAY_NAME} and building the NSIS installer"
            )
        else()
            message(STATUS "makensis not found — skipping package_windows_installer target")
        endif()
    endif()
endif()
if(APPLE)
    set(CPACK_BUNDLE_ICON "${ENCRYPTED_ICON_ICNS}")
endif()
include(CPack)
