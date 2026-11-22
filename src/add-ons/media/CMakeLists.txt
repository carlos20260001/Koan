# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Media add-ons.
#  - media-add-ons/: classic Media Kit node add-ons (libmedia), loaded by
#    media_addon_server from /system/add-ons/media;
#  - plugins/: classic Media Kit codec plugins (readers, decoders, writers,
#    encoders, streamers), loaded by libmedia's PluginManager from
#    /system/add-ons/media/plugins;
#  - equalizer/, plugin_adapter/: VitruvianOS media2 units.
# The image lists are MEDIA_ADDONS and MEDIA_PLUGINS in
# build/profiles/base.cmake.

# A classic Media Kit add-on: a bare file name (the loaders enumerate by
# directory walk), the classic headers ahead of media2's, libmedia and libbe,
# and no undefined symbol left for load time.
macro(MediaKitAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE media be root)
	UseMediaKit(${name})
	UsePrivateHeaders(${name} shared)
	target_link_options(${name} PRIVATE -Wl,--no-undefined)
endmacro()

foreach(_dir media-add-ons plugins equalizer plugin_adapter)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()

# CTest: the classic add-ons export their loader's entry point and bind to
# libmedia (MEDIA_1), not media2.
enable_testing()
set(_vos_media_plugins ffmpeg amduvd au_reader dvd_streamer http_streamer
	raw_decoder)
set(_vos_media_node_addons equalizer.media_addon esound_sink.media_addon
	mixer.media_addon pipewire.media_addon reader.media_addon
	tone_producer_demo.media_addon video_mixer.media_addon
	video_producer_demo.media_addon video_window_demo.media_addon
	vst_host.media_addon writer.media_addon)
foreach(_kind plugins node_addons)
	set(_files)
	foreach(_target IN LISTS _vos_media_${_kind})
		if(TARGET ${_target})
			list(APPEND _files "$<TARGET_FILE:${_target}>")
		endif()
	endforeach()
	if(_kind STREQUAL "plugins")
		set(_entry instantiate_plugin)
	else()
		set(_entry make_media_addon)
	endif()
	add_test(NAME media_${_kind}_entry_points
		COMMAND sh
			"${CMAKE_SOURCE_DIR}/src/tests/add-ons/media/check_entry_points.sh"
			"${CMAKE_OBJDUMP}" ${_entry} ${_files})
endforeach()
