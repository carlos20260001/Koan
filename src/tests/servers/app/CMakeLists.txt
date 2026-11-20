# app_server host tests (no window server, no nexus.ko). The test sources
# VitruvianOS keeps in src/tests/vos stay there.

add_executable(test_display_change_tracker
	${PROJECT_SOURCE_DIR}/src/tests/vos/test_display_change_tracker.cpp
	${PROJECT_SOURCE_DIR}/src/servers/app/DisplayChangeTracker.cpp
)
target_include_directories(test_display_change_tracker PRIVATE
	${PROJECT_SOURCE_DIR}/src/servers/app)
add_test(NAME app_server_display_change_tracker
	COMMAND test_display_change_tracker)

add_executable(test_screen_relayout
	test_screen_relayout.cpp
	${PROJECT_SOURCE_DIR}/src/servers/app/ScreenRelayout.cpp
)
target_include_directories(test_screen_relayout PRIVATE
	${PROJECT_SOURCE_DIR}/src/servers/app
	${PROJECT_SOURCE_DIR}/headers/private/interface
	${PROJECT_SOURCE_DIR}/headers/os/interface
	${PROJECT_SOURCE_DIR}/headers/os/support)
set_target_properties(test_screen_relayout PROPERTIES COMPILE_FLAGS
	"-include LinuxBuildCompatibility.h")
add_test(NAME app_server_screen_relayout COMMAND test_screen_relayout)
