# Classic Media Kit node add-ons (libmedia), installed to
# /system/add-ons/media (MEDIA_ADDONS in build/profiles/base.cmake; the
# PipeWire add-on installs itself). The directories that still have a Jamfile
# are not ported (PORTING.md M9, media add-ons).
foreach(_dir pipewire equalizer esound_sink mixer reader
		tone_producer_demo video_mixer video_producer_demo videowindow
		vst_host writer)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
