# wlroots-koan host regression tests (WLROOTS_KOAN_BUILD_TESTS=ON).

add_executable(koan_server_create_test koan_server_create_test.c)
target_include_directories(koan_server_create_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../server"
)
target_link_libraries(koan_server_create_test PRIVATE wlroots)
add_test(NAME koan_server_create COMMAND koan_server_create_test)

# A real libwayland-client xdg-shell toplevel against the Koan server.
pkg_check_modules(KOAN_TEST_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_package(Threads REQUIRED)
set(_koan_test_xdg_xml
    "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/stable/xdg-shell/xdg-shell.xml")
set(_koan_test_xdg_header
    "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
set(_koan_test_deco_xml
    "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
set(_koan_test_deco_header
    "${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h")
# Only client headers: the interface tables are already in libwlroots.
add_custom_command(
    OUTPUT "${_koan_test_xdg_header}"
    COMMAND "${WAYLAND_SCANNER}" client-header
        "${_koan_test_xdg_xml}" "${_koan_test_xdg_header}"
    DEPENDS "${_koan_test_xdg_xml}"
    VERBATIM
)
add_custom_command(
    OUTPUT "${_koan_test_deco_header}"
    COMMAND "${WAYLAND_SCANNER}" client-header
        "${_koan_test_deco_xml}" "${_koan_test_deco_header}"
    DEPENDS "${_koan_test_deco_xml}"
    VERBATIM
)
set(_koan_test_viewporter_xml
    "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/stable/viewporter/viewporter.xml")
set(_koan_test_viewporter_header
    "${CMAKE_CURRENT_BINARY_DIR}/viewporter-client-protocol.h")
add_custom_command(
    OUTPUT "${_koan_test_viewporter_header}"
    COMMAND "${WAYLAND_SCANNER}" client-header
        "${_koan_test_viewporter_xml}" "${_koan_test_viewporter_header}"
    DEPENDS "${_koan_test_viewporter_xml}"
    VERBATIM
)
set(_koan_test_fractional_xml
    "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/staging/fractional-scale/fractional-scale-v1.xml")
set(_koan_test_fractional_header
    "${CMAKE_CURRENT_BINARY_DIR}/fractional-scale-v1-client-protocol.h")
add_custom_command(
    OUTPUT "${_koan_test_fractional_header}"
    COMMAND "${WAYLAND_SCANNER}" client-header
        "${_koan_test_fractional_xml}" "${_koan_test_fractional_header}"
    DEPENDS "${_koan_test_fractional_xml}"
    VERBATIM
)
set(_koan_test_ctl_xml "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/koan-test-v1.xml")
set(_koan_test_ctl_header "${CMAKE_CURRENT_BINARY_DIR}/koan-test-v1-client-protocol.h")
add_custom_command(
    OUTPUT "${_koan_test_ctl_header}"
    COMMAND "${WAYLAND_SCANNER}" client-header
        "${_koan_test_ctl_xml}" "${_koan_test_ctl_header}"
    DEPENDS "${_koan_test_ctl_xml}"
    VERBATIM
)
add_executable(koan_xdg_client_test
    koan_xdg_client_test.c
    "${_koan_test_xdg_header}"
    "${_koan_test_deco_header}"
)
target_include_directories(koan_xdg_client_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../server"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(koan_xdg_client_test PRIVATE
    wlroots PkgConfig::KOAN_TEST_WAYLAND_CLIENT Threads::Threads)
add_test(NAME koan_xdg_client COMMAND koan_xdg_client_test)
set_tests_properties(koan_xdg_client PROPERTIES TIMEOUT 20)

add_executable(koan_queue_test koan_queue_test.c)
target_include_directories(koan_queue_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../server"
)
target_link_libraries(koan_queue_test PRIVATE wlroots)
add_test(NAME koan_queue COMMAND koan_queue_test)
set_tests_properties(koan_queue PROPERTIES TIMEOUT 10)

# Protocol smoke, registry and negative cases (Milestone 1 of
# docs/analysis/wayland-conformance-testing-audit.md). Prints
# RESULT|<status>|<case>|<detail> lines. Each mode runs in its own process
# so a server crash is attributed to that group alone. Cases derived from
# Weston's tests keep Weston's MIT notice in the source.
add_executable(koan_protocol_test
    koan_protocol_test.c
    "${_koan_test_xdg_header}"
    "${_koan_test_ctl_header}"
    "${_koan_test_viewporter_header}"
    "${_koan_test_fractional_header}"
)
target_include_directories(koan_protocol_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../server"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(koan_protocol_test PRIVATE
    wlroots PkgConfig::KOAN_TEST_WAYLAND_CLIENT Threads::Threads)
foreach(_koan_mode core lifecycle bad-buffer subsurface compose xdg data input testctl)
    add_test(NAME koan_protocol_${_koan_mode}
        COMMAND koan_protocol_test ${_koan_mode})
    set_tests_properties(koan_protocol_${_koan_mode} PROPERTIES TIMEOUT 20)
endforeach()

# WLCS integration (github.com/canonical/wlcs), optional and off by
# default: -DWLROOTS_KOAN_BUILD_WLCS=ON -DWLCS_INCLUDE_DIR=<wlcs>/include
# builds koan-wlcs-integration.so for `wlcs ./koan-wlcs-integration.so`.
# Both WLCS integration modules (wlcs/koan_wlcs*_integration.c) are
# GPL-3.0-only like WLCS itself (wlcs/COPYING.GPL3); the rest of
# wlroots-koan, koan_popup_follow_check.c included, stays MIT.
# See docker/wlcs/run-wlcs.sh. The module is a test artifact; it is not
# installed or packaged.
option(WLROOTS_KOAN_BUILD_WLCS "Build the WLCS integration module" OFF)
if(WLROOTS_KOAN_BUILD_WLCS)
    find_path(WLCS_INCLUDE_DIR wlcs/display_server.h
        DOC "WLCS include directory (contains wlcs/display_server.h)")
    if(NOT WLCS_INCLUDE_DIR)
        message(FATAL_ERROR "WLROOTS_KOAN_BUILD_WLCS=ON needs "
            "WLCS_INCLUDE_DIR (the include/ directory of a WLCS checkout)")
    endif()
    # The static wlroots archive is linked into a shared module.
    set_property(TARGET wlroots PROPERTY POSITION_INDEPENDENT_CODE ON)
    pkg_check_modules(KOAN_WLCS_WAYLAND REQUIRED IMPORTED_TARGET
        wayland-client wayland-server)
    add_library(koan-wlcs-integration MODULE wlcs/koan_wlcs_integration.c)
    set_target_properties(koan-wlcs-integration PROPERTIES PREFIX "")
    target_include_directories(koan-wlcs-integration PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../server"
        "${WLCS_INCLUDE_DIR}"
    )
    target_link_libraries(koan-wlcs-integration PRIVATE
        wlroots PkgConfig::KOAN_WLCS_WAYLAND)
    target_link_options(koan-wlcs-integration PRIVATE -Wl,--no-undefined)

    # The same suite against a running session (docker/wlcs/run-wlcs-vm.sh):
    # a plain Wayland client of app_server's socket, driven through
    # koan_test_v1. No wlroots inside, so it carries its own interface table.
    pkg_check_modules(KOAN_WLCS_VM_WAYLAND REQUIRED IMPORTED_TARGET
        wayland-client)
    set(_koan_test_ctl_code "${CMAKE_CURRENT_BINARY_DIR}/koan-test-v1-client-code.c")
    add_custom_command(
        OUTPUT "${_koan_test_ctl_code}"
        COMMAND "${WAYLAND_SCANNER}" private-code
            "${_koan_test_ctl_xml}" "${_koan_test_ctl_code}"
        DEPENDS "${_koan_test_ctl_xml}"
        VERBATIM
    )
    add_library(koan-wlcs-vm-integration MODULE
        wlcs/koan_wlcs_vm_integration.c
        "${_koan_test_ctl_header}"
        "${_koan_test_ctl_code}"
    )
    set_target_properties(koan-wlcs-vm-integration PROPERTIES PREFIX "")
    target_include_directories(koan-wlcs-vm-integration PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${WLCS_INCLUDE_DIR}"
    )
    target_link_libraries(koan-wlcs-vm-integration PRIVATE
        PkgConfig::KOAN_WLCS_VM_WAYLAND)
    target_link_options(koan-wlcs-vm-integration PRIVATE -Wl,--no-undefined)

    # Manual VM check: popups follow their toplevel (see the file header).
    set(_koan_test_xdg_code "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-code.c")
    add_custom_command(
        OUTPUT "${_koan_test_xdg_code}"
        COMMAND "${WAYLAND_SCANNER}" private-code
            "${_koan_test_xdg_xml}" "${_koan_test_xdg_code}"
        DEPENDS "${_koan_test_xdg_xml}"
        VERBATIM
    )
    add_executable(koan-popup-follow-check
        wlcs/koan_popup_follow_check.c
        "${_koan_test_ctl_header}" "${_koan_test_ctl_code}"
        "${_koan_test_xdg_header}" "${_koan_test_xdg_code}"
    )
    target_include_directories(koan-popup-follow-check PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(koan-popup-follow-check PRIVATE
        PkgConfig::KOAN_WLCS_VM_WAYLAND)
endif()
