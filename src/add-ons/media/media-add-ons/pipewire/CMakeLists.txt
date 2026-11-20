# The PipeWire node add-on: PipeWire's sinks and sources as the classic
# Media Kit's physical outputs and inputs (in place of Haiku's multi_audio
# driver nodes), loaded by media_addon_server. It uses libpipewire
# directly; the classic Media Kit (libmedia, MEDIA_1) is the only media
# library it links.
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

AddOn(
	pipewire.media_addon
	SHARED

	SOURCES
	PipeWireAddOn.cpp
	PipeWireGraph.cpp
	PipeWireNode.cpp

	LIBS media be root ${PIPEWIRE_LIBRARIES}
)

set_target_properties(pipewire.media_addon PROPERTIES PREFIX "" SUFFIX "")
UseMediaKit(pipewire.media_addon)
UsePrivateHeaders(pipewire.media_addon media shared)
target_include_directories(pipewire.media_addon PRIVATE
	${PIPEWIRE_INCLUDE_DIRS})
target_compile_options(pipewire.media_addon PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
target_link_options(pipewire.media_addon PRIVATE -Wl,--no-undefined)
install(TARGETS pipewire.media_addon
	DESTINATION /system/add-ons/media COMPONENT runtime)

# CTest: the PipeWire side alone (no Media Kit). It needs a running
# PipeWire (and a session manager for the devices); without one it is
# skipped (exit code 77).
add_executable(pipewire_graph_test
	tests/PipeWireGraphTest.cpp
	PipeWireGraph.cpp)
target_include_directories(pipewire_graph_test PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR} ${PIPEWIRE_INCLUDE_DIRS})
target_compile_options(pipewire_graph_test PRIVATE ${PIPEWIRE_CFLAGS_OTHER})
target_link_libraries(pipewire_graph_test PRIVATE ${PIPEWIRE_LIBRARIES})
enable_testing()
add_test(NAME pipewire_graph COMMAND pipewire_graph_test)
set_tests_properties(pipewire_graph PROPERTIES SKIP_RETURN_CODE 77)
