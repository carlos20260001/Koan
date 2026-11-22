add_subdirectory(devices)
add_subdirectory(filters)
add_subdirectory(methods)

# CTest: the add-ons export the instantiate_input_<kind> function
# input_server's AddOnManager looks up.
enable_testing()
add_test(NAME input_server_addons_entry_points
	COMMAND sh -c "sh ${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh ${CMAKE_OBJDUMP} instantiate_input_filter -- $<TARGET_FILE:accessibility_keys> $<TARGET_FILE:padblocker> $<TARGET_FILE:minimize_all> $<TARGET_FILE:screen_saver> $<TARGET_FILE:switch_workspace> && sh ${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh ${CMAKE_OBJDUMP} instantiate_input_method -- $<TARGET_FILE:pen> $<TARGET_FILE:t9> && sh ${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh ${CMAKE_OBJDUMP} instantiate_input_device -- $<TARGET_FILE:keyboard> $<TARGET_FILE:mouse> $<TARGET_FILE:virtualkeyboard>")
