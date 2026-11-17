# Registers players with the media_controls_server and checks the active
# player and the routing of actions. Run it on the system under test, with
# no other player registered; built, not registered with CTest.
Test(media_session_test
	SOURCES media_session_test.cpp
	LIBS media_controls
)
target_include_directories(media_session_test PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/media_controls)
