# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# mail_daemon's protocol and filter add-ons, on air/OS's mail kit
# (libmail). BMailSettings finds them by file name under
# add-ons/mail_daemon/<kind>, so they have no "lib" prefix or ".so" suffix.
if(NOT TARGET mail)
	message(STATUS "mail_daemon add-ons: skipped, needs the mail kit")
	return()
endif()

macro(MailDaemonAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE mail be root localestub)
	target_link_options(${name} PRIVATE "-Wl,--no-undefined")
	UsePrivateHeaders(${name} shared)
	# UsePrivateHeaders has no "mail" entry.
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/private/mail
		${PROJECT_SOURCE_DIR}/headers/os/add-ons/mail_daemon)
endmacro()

add_subdirectory(inbound_filters)
add_subdirectory(inbound_protocols)
add_subdirectory(outbound_filters)
add_subdirectory(outbound_protocols)

# CTest: the add-ons export what mail_daemon and the E-mail preference
# look up.
enable_testing()
add_test(NAME mail_daemon_inbound_protocols_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_inbound_protocol instantiate_protocol_settings_view
		-- $<TARGET_FILE:IMAP> $<TARGET_FILE:POP3>)
add_test(NAME mail_daemon_outbound_protocols_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_outbound_protocol instantiate_protocol_settings_view
		-- $<TARGET_FILE:SMTP>)
add_test(NAME mail_daemon_filters_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_filter instantiate_filter_settings_view
		-- $<TARGET_FILE:MatchHeader> $<TARGET_FILE:NewMailNotification>
		$<TARGET_FILE:SpamFilter> $<TARGET_FILE:Fortune>)
