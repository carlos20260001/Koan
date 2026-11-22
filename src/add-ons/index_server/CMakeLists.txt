# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# index_server's analysers. The server loads every add-on in
# add-ons/index_server and asks it for instantiate_index_server_addon();
# each add-on carries its own copy of IndexServerAddOn.cpp, as on Jam.
macro(IndexServerAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_sources(${name} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../IndexServerAddOn.cpp)
	target_link_libraries(${name} PRIVATE be root)
	target_link_options(${name} PRIVATE "-Wl,--no-undefined")
	UsePrivateHeaders(${name} shared storage)
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/private/index_server)
endmacro()

add_subdirectory(audiotags)
add_subdirectory(fulltext)

# CTest: what index_server looks up in each add-on.
enable_testing()
add_test(NAME index_server_addons_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_index_server_addon
		-- $<TARGET_FILE:AudioTagAnalyser> $<TARGET_FILE:FullTextAnalyser>)
