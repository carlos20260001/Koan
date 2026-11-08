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
# Only the client header: the interface tables are already in libwlroots.
add_custom_command(
    OUTPUT "${_koan_test_xdg_header}"
    COMMAND "${WAYLAND_SCANNER}" client-header
        "${_koan_test_xdg_xml}" "${_koan_test_xdg_header}"
    DEPENDS "${_koan_test_xdg_xml}"
    VERBATIM
)
add_executable(koan_xdg_client_test
    koan_xdg_client_test.c
    "${_koan_test_xdg_header}"
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
