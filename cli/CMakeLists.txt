##
## cli/CMakeLists.txt - Build dsview-cli (headless command-line bridge)
##
## This target reuses libsigrok4DSL/libsigrokdecode4DSL/common sources from
## the parent DSView tree. It can be built either from the main DSView root or
## as a standalone cli-only entry via `cmake -S external/DSView/cli`.
##

cmake_minimum_required(VERSION 3.10)

get_filename_component(DSVIEW_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(DSVIEW_CLI_STANDALONE OFF)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(DSVIEW_CLI_STANDALONE ON)
    project(dsview-cli LANGUAGES C)
endif()

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${DSVIEW_ROOT_DIR}/CMake")

if(DSVIEW_CLI_STANDALONE OR
   NOT DEFINED libsigrok4DSL_SOURCES OR
   NOT DEFINED libsigrokdecode4DSL_SOURCES OR
   NOT DEFINED common_SOURCES)
    include("${CMAKE_CURRENT_LIST_DIR}/dsview_cli_sources.cmake")
endif()

function(dsview_cli_prefix_sources out_var source_root)
    set(_prefixed_sources "")
    foreach(_src ${ARGN})
        list(APPEND _prefixed_sources "${source_root}/${_src}")
    endforeach()
    set(${out_var} "${_prefixed_sources}" PARENT_SCOPE)
endfunction()

function(dsview_cli_resolve_python)
    find_package(Python3 COMPONENTS Interpreter Development QUIET)

    if(Python3_FOUND)
        set(PY_LIB ${Python3_LIBRARIES} PARENT_SCOPE)
        set(DS_PYTHON_INCLUDE_DIR ${Python3_INCLUDE_DIRS} PARENT_SCOPE)
        set(DS_PYTHON_VERSION_MAJOR ${Python3_VERSION_MAJOR} PARENT_SCOPE)
        set(DS_PYTHON_VERSION_MINOR ${Python3_VERSION_MINOR} PARENT_SCOPE)
        return()
    endif()

    find_package(PythonLibs 3 REQUIRED QUIET)
    set(PY_LIB ${PYTHON_LIBRARIES} PARENT_SCOPE)
    set(DS_PYTHON_INCLUDE_DIR ${PYTHON_INCLUDE_DIRS} PARENT_SCOPE)
    string(REPLACE "." ";" _python_version_parts "${PYTHONLIBS_VERSION_STRING}")
    list(GET _python_version_parts 0 _python_major)
    list(GET _python_version_parts 1 _python_minor)
    set(DS_PYTHON_VERSION_MAJOR ${_python_major} PARENT_SCOPE)
    set(DS_PYTHON_VERSION_MINOR ${_python_minor} PARENT_SCOPE)
endfunction()

if(DSVIEW_CLI_STANDALONE)
    include(FindPkgConfig)

    find_package(PkgConfig REQUIRED)

    set(STATIC_PKGDEPS_LIBS FALSE)
    if(WIN32)
        set(STATIC_PKGDEPS_LIBS TRUE)
    endif()

    pkg_search_module(GLIB REQUIRED glib-2.0)
    if(WIN32 AND STATIC_PKGDEPS_LIBS)
        add_definitions(-DGLIB_STATIC_COMPILATION -DGOBJECT_STATIC_COMPILATION -DGIO_STATIC_COMPILATION)
    endif()

    dsview_cli_resolve_python()
    find_package(libusb-1.0 REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)
endif()

dsview_cli_prefix_sources(_cli_lib_sources "${DSVIEW_ROOT_DIR}" ${libsigrok4DSL_SOURCES})
dsview_cli_prefix_sources(_cli_common_sources "${DSVIEW_ROOT_DIR}" ${common_SOURCES})
dsview_cli_prefix_sources(_cli_decode_sources "${DSVIEW_ROOT_DIR}" ${libsigrokdecode4DSL_SOURCES})

set(DSVIEW_CLI_MODULE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/command"
    "${CMAKE_CURRENT_LIST_DIR}/runtime"
    "${CMAKE_CURRENT_LIST_DIR}/device"
    "${CMAKE_CURRENT_LIST_DIR}/source"
    "${CMAKE_CURRENT_LIST_DIR}/waveform"
    "${CMAKE_CURRENT_LIST_DIR}/decode"
    "${CMAKE_CURRENT_LIST_DIR}/support"
)

add_executable(dsview-cli
    command/main.c
    command/options.c
    command/shape.c
    command/parse.c
    runtime/runtime_layout.c
    device/device_selected.c
    device/device_config.c
    source/logic_source.c
    command/scan_run.c
    command/show_run.c
    command/get_run.c
    command/set_run.c
    command/capture_run.c
    command/export_run.c
    waveform/srzip_session_conversion.c
    waveform/session_metadata.c
    waveform/waveform_session.c
    waveform/waveform_archive_output.c
    waveform/waveform_archive_output_none.c
    waveform/waveform_archive_output_srzip.c
    waveform/waveform_archive_output_dsl.c
    source/channel_selection_state.c
    decode/stack_plan.c
    command/decode_run.c
    decode/runtime.c
    decode/stack.c
    decode/feed.c
    decode/export.c
    decode/summary.c
    support/command_result_json.c
    support/json.c
    ${_cli_lib_sources}
    ${_cli_decode_sources}
    ${_cli_common_sources}
)

if(WIN32 AND DSVIEW_CLI_POST_BUILD_STAGE_SCRIPT)
    add_custom_command(TARGET dsview-cli POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -P "${DSVIEW_CLI_POST_BUILD_STAGE_SCRIPT}"
        COMMENT "Stage DSView CLI runtime into build.dir")
endif()

add_executable(dsview-cli-command-shape-test
    tests/command_shape_test.c
    tests/libsigrok_parse_stubs.c
    command/shape.c
    command/options.c
)

add_executable(dsview-cli-decode-stack-plan-test
    tests/decode_stack_plan_test.c
    tests/libsigrokdecode_decoder_list_stubs.c
    decode/stack_plan.c
    decode/export.c
    command/parse.c
)

add_executable(dsview-cli-waveform-archive-output-test
    tests/waveform_archive_output_test.c
    tests/libsigrok_waveform_archive_stubs.c
    waveform/waveform_archive_output.c
    waveform/waveform_archive_output_none.c
    waveform/waveform_archive_output_srzip.c
    waveform/waveform_archive_output_dsl.c
    waveform/session_metadata.c
    source/channel_selection_state.c
    support/json.c
    ${DSVIEW_ROOT_DIR}/common/minizip/ioapi.c
    ${DSVIEW_ROOT_DIR}/common/minizip/zip.c
    ${DSVIEW_ROOT_DIR}/common/minizip/unzip.c
)

add_executable(dsview-cli-srzip-session-conversion-test
    tests/srzip_session_conversion_test.c
    tests/libsigrok_waveform_archive_stubs.c
    tests/libsigrok_parse_stubs.c
    waveform/srzip_session_conversion.c
    waveform/waveform_archive_output.c
    waveform/waveform_archive_output_none.c
    waveform/waveform_archive_output_srzip.c
    waveform/waveform_archive_output_dsl.c
    waveform/session_metadata.c
    source/channel_selection_state.c
    support/json.c
    ${DSVIEW_ROOT_DIR}/common/minizip/ioapi.c
    ${DSVIEW_ROOT_DIR}/common/minizip/zip.c
    ${DSVIEW_ROOT_DIR}/common/minizip/unzip.c
)

add_executable(dsview-cli-decode-export-test
    tests/decode_export_test.c
    decode/export.c
)

add_executable(dsview-cli-decode-summary-test
    tests/decode_summary_test.c
    decode/summary.c
    support/json.c
)

add_executable(dsview-cli-channel-selection-state-test
    tests/channel_selection_state_test.c
    source/channel_selection_state.c
)

add_executable(dsview-cli-device-config-test
    tests/device_config_test.c
    tests/libsigrok_parse_stubs.c
    command/parse.c
    device/device_config.c
)

target_include_directories(dsview-cli PRIVATE
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/libsigrokdecode4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${LIBUSB_1_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
    ${DS_PYTHON_INCLUDE_DIR}
)

target_include_directories(dsview-cli-command-shape-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_include_directories(dsview-cli-decode-stack-plan-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/libsigrokdecode4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_include_directories(dsview-cli-waveform-archive-output-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
)

target_include_directories(dsview-cli-srzip-session-conversion-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
)

target_include_directories(dsview-cli-decode-export-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/libsigrokdecode4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_include_directories(dsview-cli-decode-summary-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrokdecode4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_include_directories(dsview-cli-channel-selection-state-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_include_directories(dsview-cli-device-config-test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DSVIEW_ROOT_DIR}"
    "${DSVIEW_ROOT_DIR}/DSView"
    "${DSVIEW_ROOT_DIR}/libsigrok4DSL"
    "${DSVIEW_ROOT_DIR}/common"
    ${DSVIEW_CLI_MODULE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

target_compile_definitions(dsview-cli PRIVATE
    HAVE_LIBUSB_1_0=1
    HAVE_DSL_DEVICE=1
    HAVE_LA_DEMO=1
)

target_compile_definitions(dsview-cli-command-shape-test PRIVATE
    HAVE_DSL_DEVICE=1
)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(dsview-cli PRIVATE _DEFAULT_SOURCE)
endif()

set_target_properties(dsview-cli PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-command-shape-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-decode-stack-plan-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-waveform-archive-output-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-srzip-session-conversion-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-decode-export-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-decode-summary-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-channel-selection-state-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

set_target_properties(dsview-cli-device-config-test PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${DSVIEW_ROOT_DIR}/build.dir"
)

if(NOT MSVC)
    target_compile_options(dsview-cli PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-command-shape-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-decode-stack-plan-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-waveform-archive-output-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-srzip-session-conversion-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-decode-export-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-decode-summary-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-channel-selection-state-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
    target_compile_options(dsview-cli-device-config-test PRIVATE
        -Wno-unused-result
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-implicit-function-declaration
    )
endif()

set(DSVIEW_CLI_LINK_LIBS
    ${CMAKE_THREAD_LIBS_INIT}
    ${LIBUSB_1_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${PY_LIB}
)

if(NOT MSVC)
    list(APPEND DSVIEW_CLI_LINK_LIBS m)
endif()

if(STATIC_PKGDEPS_LIBS)
    link_directories(${GLIB_STATIC_LIBRARY_DIRS})
    list(APPEND DSVIEW_CLI_LINK_LIBS "-Wl,-Bstatic" ${GLIB_STATIC_LIBRARIES} iconv "-Wl,-Bdynamic")
    if(WIN32)
        list(APPEND DSVIEW_CLI_LINK_LIBS setupapi advapi32)
    endif()
else()
    if(GLIB_LIBRARY_DIRS)
        link_directories(${GLIB_LIBRARY_DIRS})
    endif()
    list(APPEND DSVIEW_CLI_LINK_LIBS ${GLIB_LIBRARIES})
endif()

target_link_libraries(dsview-cli ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-command-shape-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-decode-stack-plan-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-waveform-archive-output-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-srzip-session-conversion-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-decode-export-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-decode-summary-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-channel-selection-state-test ${DSVIEW_CLI_LINK_LIBS})
target_link_libraries(dsview-cli-device-config-test ${DSVIEW_CLI_LINK_LIBS})

if(WIN32 AND STATIC_PKGDEPS_LIBS)
    set_target_properties(dsview-cli PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-command-shape-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-decode-stack-plan-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-waveform-archive-output-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-srzip-session-conversion-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-decode-export-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-decode-summary-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-channel-selection-state-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
    set_target_properties(dsview-cli-device-config-test PROPERTIES LINK_FLAGS "-static -static-libgcc")
endif()

add_test(NAME dsview-cli-command-shape-test
    COMMAND dsview-cli-command-shape-test)
add_test(NAME dsview-cli-decode-stack-plan-test
    COMMAND dsview-cli-decode-stack-plan-test)
add_test(NAME dsview-cli-waveform-archive-output-test
    COMMAND dsview-cli-waveform-archive-output-test)
add_test(NAME dsview-cli-srzip-session-conversion-test
    COMMAND dsview-cli-srzip-session-conversion-test)
add_test(NAME dsview-cli-decode-export-test
    COMMAND dsview-cli-decode-export-test)
add_test(NAME dsview-cli-decode-summary-test
    COMMAND dsview-cli-decode-summary-test)
add_test(NAME dsview-cli-channel-selection-state-test
    COMMAND dsview-cli-channel-selection-state-test)
add_test(NAME dsview-cli-device-config-test
    COMMAND dsview-cli-device-config-test)

install(TARGETS dsview-cli DESTINATION ${CMAKE_INSTALL_BINDIR})

if(DSVIEW_CLI_STANDALONE)
    install(DIRECTORY "${DSVIEW_ROOT_DIR}/DSView/res" DESTINATION ${CMAKE_INSTALL_DATADIR}/DSView)
    install(DIRECTORY "${DSVIEW_ROOT_DIR}/DSView/demo" DESTINATION ${CMAKE_INSTALL_DATADIR}/DSView)

    if(WIN32)
        install(DIRECTORY "${DSVIEW_ROOT_DIR}/libsigrokdecode4DSL/decoders"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/libsigrokdecode4DSL
            PATTERN "ir_irmp" EXCLUDE)
    else()
        install(DIRECTORY "${DSVIEW_ROOT_DIR}/libsigrokdecode4DSL/decoders"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/libsigrokdecode4DSL)
    endif()

    if(WIN32)
        message(STATUS "[DEBUG-2a3b] Python3_EXECUTABLE=${Python3_EXECUTABLE}")
        message(STATUS "[DEBUG-2a3b] Python3_INCLUDE_DIRS=${Python3_INCLUDE_DIRS}")
        message(STATUS "[DEBUG-2a3b] Python3_RUNTIME_LIBRARY_DIRS=${Python3_RUNTIME_LIBRARY_DIRS}")
        set(_ds_python_include_dir ${DS_PYTHON_INCLUDE_DIR})
        list(GET _ds_python_include_dir 0 _ds_python_include_dir)
        get_filename_component(DS_PYTHON_INCLUDE_DIR_ABS "${_ds_python_include_dir}" ABSOLUTE)
        get_filename_component(DS_PYTHON_PREFIX "${DS_PYTHON_INCLUDE_DIR_ABS}" DIRECTORY)
        get_filename_component(DS_PYTHON_PREFIX "${DS_PYTHON_PREFIX}" DIRECTORY)
        # Anchor runtime file lookups on the directory of the Python
        # executable that CMake actually linked against. The include-dir-only
        # derivation breaks for installs where the layout adds an extra
        # directory level (e.g. the GitHub-hosted toolcache at .../x64/).
        if(Python3_EXECUTABLE)
            get_filename_component(DS_PYTHON_BIN_DIR "${Python3_EXECUTABLE}" DIRECTORY)
        else()
            set(DS_PYTHON_BIN_DIR "${DS_PYTHON_PREFIX}/bin")
        endif()
        set(DS_PYTHON_LIB_DIR "${DS_PYTHON_PREFIX}/lib")
        set(DS_PYTHON_STDLIB_ZIP "${DS_PYTHON_LIB_DIR}/python${DS_PYTHON_VERSION_MAJOR}${DS_PYTHON_VERSION_MINOR}.zip")
        set(DS_PYTHON_STDLIB_DIR "${DS_PYTHON_LIB_DIR}/python${DS_PYTHON_VERSION_MAJOR}.${DS_PYTHON_VERSION_MINOR}")
        set(DS_RUNTIME_DLLS
            python${DS_PYTHON_VERSION_MAJOR}${DS_PYTHON_VERSION_MINOR}.dll
            libpython${DS_PYTHON_VERSION_MAJOR}.${DS_PYTHON_VERSION_MINOR}.dll
            libgcc_s_seh-1.dll
            libffi-8.dll
            zlib1.dll
            libglib-2.0-0.dll
            libwinpthread-1.dll
            libintl-8.dll
            libpcre2-8-0.dll
            libiconv-2.dll)

        foreach(DS_RUNTIME_DLL ${DS_RUNTIME_DLLS})
            if(EXISTS "${DS_PYTHON_BIN_DIR}/${DS_RUNTIME_DLL}")
                install(FILES "${DS_PYTHON_BIN_DIR}/${DS_RUNTIME_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
            endif()
        endforeach()

        if(EXISTS "${DS_PYTHON_STDLIB_ZIP}")
            install(FILES "${DS_PYTHON_STDLIB_ZIP}" DESTINATION ${CMAKE_INSTALL_BINDIR}/Python/lib)
        endif()

        if(EXISTS "${DS_PYTHON_STDLIB_DIR}")
            install(DIRECTORY "${DS_PYTHON_STDLIB_DIR}" DESTINATION ${CMAKE_INSTALL_BINDIR}/Python/lib
                PATTERN "site-packages" EXCLUDE)
        endif()

        set(DS_BUILD_STAGE_DIR "${DSVIEW_ROOT_DIR}/build.dir")
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/stage_build_dir_cli.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/stage_build_dir.cmake"
            @ONLY)
        add_custom_target(stage-build-dir ALL
            COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/stage_build_dir.cmake"
            DEPENDS dsview-cli
            COMMENT "Stage dsview-cli runtime into build.dir")
    endif()
endif()
