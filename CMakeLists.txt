cmake_minimum_required(VERSION 3.21)
project(rename LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 32-bit builds are not supported: the plugin ships and is tested as x64, and a
# Wow64 process would be an untested configuration for IFileOperation shell
# work. Refuse rather than build something nobody verified.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ra9fael.rename must be built as x64 (pointer size 8)")
endif()

# ---- the Qt Quick window (opt-in) --------------------------------------------
# Qt is not installed on every machine that builds this plugin, and the plugin is
# fully functional without it: rename.exe answers the host protocol and the whole
# pipeline is exercised by the pure-C++ tests. Enable this to get the window.
#
# Declared here rather than beside the Qt targets because it also decides the CRT
# for every target: Qt's DLLs are built with the dynamic CRT, and MSVC refuses to
# link a static-CRT object into a dynamic-CRT image (LNK2038). One switch, so the
# two can never disagree.
option(RENAME_BUILD_QT "Build the Qt Widgets window (requires Qt 6.5+)" OFF)

if(RENAME_BUILD_QT)
    set(RENAME_CRT "MultiThreadedDLL$<$<CONFIG:Debug>:MultiThreadedDebugDLL>")
else()
    # Static CRT: no runtime DLL for the sandbox to refuse to load, and normal
    # mode starts faster.
    set(RENAME_CRT "MultiThreaded$<$<CONFIG:Debug>:MultiThreadedDebug>")
endif()

# ---- rename_core: pure logic, no Windows and no Qt headers --------------------
# This is a hard architectural constraint (DEVELOPMENT-PLAN.md §4): the L1 tests
# must build and run on any platform, and both the control-mode rule engine and
# the oil-mode parser must share the finalization pipeline. Anything that needs
# <windows.h> or Qt belongs in rename_win or rename_ui instead.
add_library(rename_core STATIC
    core/name_split.cpp
    core/finalize.cpp
    core/case_transform.cpp
    core/status.cpp
    core/template.cpp
    core/counter.cpp
    core/rule_engine.cpp
    core/oil_parser.cpp
    core/oil_differ.cpp
    core/plan.cpp
)
target_include_directories(rename_core PUBLIC "${CMAKE_SOURCE_DIR}")
target_compile_definitions(rename_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(rename_core PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(rename_core PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
else()
    target_compile_options(rename_core PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()

# ---- rename.exe: the resident plugin -----------------------------------------
# plugin.json names this exe as runtime.program, so CMake writes it into the
# plugin folder. The resident entry point lives in main.cpp (M3); it also writes the
# P1 probe log to stderr, so the iles trigger can be re-checked after changes.
add_library(rename_win STATIC
    win/enumerate.cpp
    win/execute.cpp
    win/metadata.cpp
    win/nfc.cpp
    win/template_context.cpp
)
target_link_libraries(rename_win PUBLIC rename_core)
target_compile_definitions(rename_win PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(rename_win PRIVATE shell32 ole32 shlwapi windowscodecs propsys)
if(MSVC)
    target_compile_options(rename_win PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(rename_win PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
else()
    target_compile_options(rename_win PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()

add_executable(rename main.cpp)
if(WIN32)
    target_sources(rename PRIVATE resources/rename.rc)
endif()
target_link_libraries(rename PRIVATE rename_core rename_win rename_ui)
target_include_directories(rename PRIVATE "${CMAKE_SOURCE_DIR}")
# The view-model is plain C++ with no Qt, which is what makes the UI behaviour
# testable on a machine with no Qt installed. The Qt/QML layer that renders it is
# an optional target (see RENAME_BUILD_QT below).
add_library(rename_ui STATIC
    ui/presenter.cpp
)
target_include_directories(rename_ui PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(rename_ui PUBLIC rename_core)
target_compile_definitions(rename_ui PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(rename_ui PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(rename_ui PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
else()
    target_compile_options(rename_ui PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()
target_compile_definitions(rename PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(rename PRIVATE /utf-8 /W4 /permissive-)
    # Static CRT: no runtime DLL for the sandbox to refuse to load, and normal
    # mode starts faster.
    set_target_properties(rename PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
else()
    target_compile_options(rename PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
    target_link_options(rename PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(rename PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(rename PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
endforeach()

# ---- tests -------------------------------------------------------------------
# The workspace convention (ra9fael.volume, cursorwrap, bright) is a zero-dependency
# Catch2-style harness in tests/test_harness.h rather than an external framework.
# rename_core has no OS dependency, so this target links only the C runtime.
add_executable(rename_tests tests/test_core.cpp tests/test_presenter.cpp)
target_link_libraries(rename_tests PRIVATE rename_core rename_ui)
target_compile_definitions(rename_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(rename_tests PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(rename_tests PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
else()
    target_compile_options(rename_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()
set_target_properties(rename_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(rename_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

# ---- optional Qt Widgets layer ----------------------------------------------
# The window lives inside rename.exe rather than in a second exe, because
# plugin.json names one runtime.program and the shared-Qt runner starts that. The
# structure mirrors ra9fael.envhosts: the Qt event loop owns the main thread, the
# JSON-RPC loop moves to a worker. This experiment uses Widgets instead of QML:
# no QML engine and no scene graph, at the cost of Qt6Widgets.dll not being in
# WinShun's shared-Qt contract yet (bundle it during the experiment).
#
# RENAME_BUILD_QT and RENAME_CRT are declared at the top: the switch decides the
# CRT for every target, so it has to be known before the first one is defined.
if(RENAME_BUILD_QT)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
    qt_standard_project_setup()

    # ---- vimcore: the portable modal editing core ---------------------------
    # A git submodule (vendor/vimcore, github.com/ra9fael/winshun-vimcore) with
    # no Qt or Win32 dependency of its own; only the Qt build needs it, because
    # only the text editor uses it. `--recurse-submodules` is documented in the
    # README: without the submodule the file is simply absent and this branch
    # fails loudly instead of silently dropping vim mode.
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/vendor/vimcore/CMakeLists.txt")
        message(FATAL_ERROR
            "vendor/vimcore is missing. Run: git submodule update --init --recursive")
    endif()
    set(VIMCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(vendor/vimcore EXCLUDE_FROM_ALL)

    add_library(rename_widgets STATIC
        ui/bridge.cpp
        ui/qt_host.cpp
        ui/vim_qt.cpp
        ui/widgets_window.cpp
        ui/widgets_theme.cpp
    )
    target_include_directories(rename_widgets PUBLIC "${CMAKE_SOURCE_DIR}")
    target_link_libraries(rename_widgets PUBLIC rename_core rename_ui rename_win vimcore)
    target_link_libraries(rename_widgets PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
    if(WIN32)
        target_link_libraries(rename_widgets PUBLIC dwmapi)
    endif()
    set_target_properties(rename_widgets PROPERTIES AUTOMOC ON)
    qt_add_resources(rename_widgets "rename_icons"
        PREFIX "/"
        BASE "${CMAKE_SOURCE_DIR}"
        FILES
            resources/icons/app-16.png
            resources/icons/Phosphor.ttf
            resources/icons/app-256.png
    )
    if(MSVC)
        target_compile_options(rename_widgets PRIVATE /utf-8 /W4 /permissive-)
        set_target_properties(rename_widgets PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
    endif()

    # main.cpp gains the window path; RENAME_HAVE_QT is what selects it there.
    target_link_libraries(rename PRIVATE rename_widgets)
    target_compile_definitions(rename PRIVATE RENAME_HAVE_QT)

    # ---- vim adapter tests (Qt only) -----------------------------------------
    # vimcore's own suite is portable and lives in the submodule; this target
    # proves the Qt glue: key translation, offset mapping, insert-mode pass-through.
    add_executable(vim_qt_tests tests/test_vim_qt.cpp)
    target_link_libraries(vim_qt_tests PRIVATE rename_widgets Qt6::Core Qt6::Gui Qt6::Widgets)
    set_target_properties(vim_qt_tests PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(vim_qt_tests PRIVATE /utf-8 /W4 /permissive-)
        set_target_properties(vim_qt_tests PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
    endif()
    add_test(NAME vim_qt COMMAND vim_qt_tests)

    add_executable(vim_window_tests tests/test_vim_window.cpp)
    target_link_libraries(vim_window_tests PRIVATE rename_widgets Qt6::Core Qt6::Gui Qt6::Widgets)
    set_target_properties(vim_window_tests PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(vim_window_tests PRIVATE /utf-8 /W4 /permissive-)
        set_target_properties(vim_window_tests PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
    endif()
    add_test(NAME vim_window COMMAND vim_window_tests)

    # ---- standalone window wrapper (local verification only) ----------------
    # Not shipped: plugin.json names rename.exe, and only one program may be
    # runtime.program. This executable exists so the window can be opened and
    # driven on this machine with no host, no protocol and no .wsplugin:
    #
    #   rename-ui.exe --dir <folder> [--recursive]
    #   rename-ui.exe --files <path> [<path>...]
    #
    # It links the same rename_widgets and rename_win, so the code path under test
    # is the plugin's own.
    add_executable(rename_ui_exe ui/main-ui.cpp)
    if(WIN32)
        target_sources(rename_ui_exe PRIVATE resources/rename.rc)
    endif()
    target_link_libraries(rename_ui_exe PRIVATE rename_widgets rename_win)
    set_target_properties(rename_ui_exe PROPERTIES AUTOMOC ON OUTPUT_NAME "rename-ui")
    if(MSVC)
        set_target_properties(rename_ui_exe PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
    endif()
    set_target_properties(rename_ui_exe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(rename_ui_exe PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
    endforeach()
else()
    message(STATUS "RENAME_BUILD_QT=OFF: building without the Qt Widgets window")
endif()

enable_testing()
add_test(NAME rename_core COMMAND rename_tests)

# The end-to-end check mutates a temp tree and needs a COM apartment, so it is a
# separate executable. It is registered as a test because it cleans up after
# itself and is the only test that proves the executor really renames.
add_executable(rename_e2e tests/e2e_rename.cpp)
target_link_libraries(rename_e2e PRIVATE rename_core rename_win rename_ui)
target_compile_definitions(rename_e2e PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC)
    target_compile_options(rename_e2e PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(rename_e2e PROPERTIES MSVC_RUNTIME_LIBRARY "${RENAME_CRT}")
endif()
add_test(NAME rename_e2e COMMAND rename_e2e)

# ---- architectural gate -------------------------------------------------------
# core/ must stay free of Windows and Qt headers (DEVELOPMENT-PLAN.md §4). This
# is registered as a test so a violation fails the build instead of relying on
# someone remembering to check.
find_program(PWSH_EXECUTABLE NAMES pwsh)
if(PWSH_EXECUTABLE)
    add_test(NAME core_purity
             COMMAND "${PWSH_EXECUTABLE}" -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/check_core_purity.ps1")
    # The same kind of gate for the protocol side: a row's own fields live in
    # `data`, and the host hands the item back as it came, so action() that reads
    # `a.item["batch"]` answers "No files selected" to a row that carried one.
    # TEST-SPEC.md LS-S10-01…03.
    add_test(NAME action_payload
             COMMAND "${PWSH_EXECUTABLE}" -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/check-action-payload.ps1")
else()
    message(WARNING "pwsh not found: the core/ purity gate is not registered as a test")
endif()
