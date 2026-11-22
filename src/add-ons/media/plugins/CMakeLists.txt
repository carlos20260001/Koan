# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Classic Media Kit codec plugins, installed to /system/add-ons/media/plugins
# (MEDIA_PLUGINS in build/profiles/base.cmake).
foreach(_dir ffmpeg amdgpu_va au_reader dvd_streamer http_streamer
		raw_decoder)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
