# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Printer driver add-ons (add-ons/Print) and transport add-ons
# (add-ons/Print/transport), loaded by print_server and
# print_addon_server for printers that are not CUPS queues. The file name
# is what a printer folder records as its driver or transport, so it has
# no "lib" prefix or ".so" suffix.
macro(PrintAddOn target output)
	AddOn(${target} SHARED ${ARGN})
	set_target_properties(${target} PROPERTIES
		PREFIX "" SUFFIX "" OUTPUT_NAME "${output}")
	target_link_libraries(${target} PRIVATE be root)
	target_link_options(${target} PRIVATE "-Wl,--no-undefined")
endmacro()

add_subdirectory(drivers)
add_subdirectory(transports)

# CTest: the drivers and transports export what print_server and
# print_addon_server look up.
enable_testing()
set(_print_drivers print_lips3 print_lips4 print_gutenprint print_lpstyl
	print_pcl5 print_pcl6 print_ps print_preview)
set(_print_transports print_jetdirect print_ipp print_lpr print_parallel
	print_to_file print_serial print_usb)
set(_print_driver_files)
foreach(_target IN LISTS _print_drivers)
	list(APPEND _print_driver_files "$<TARGET_FILE:${_target}>")
endforeach()
set(_print_transport_files)
foreach(_target IN LISTS _print_transports)
	list(APPEND _print_transport_files "$<TARGET_FILE:${_target}>")
endforeach()
add_test(NAME print_drivers_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		add_printer config_page config_job take_job -- ${_print_driver_files})
add_test(NAME print_transports_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		init_transport exit_transport -- ${_print_transport_files})
# The transports with a port menu in the Printers preference also list
# their ports; the network and file transports ask in a setup dialog.
add_test(NAME print_transports_list_ports
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		list_transport_ports
		-- $<TARGET_FILE:print_ipp> $<TARGET_FILE:print_parallel>
		$<TARGET_FILE:print_serial> $<TARGET_FILE:print_usb>)
