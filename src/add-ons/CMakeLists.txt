# Add-ons. Only subdirectories that are on CMake are added; the others keep
# their Jamfiles until their wave ports them (PORTING.md M9).
#
# src/CMakeLists.txt reaches this file with or without libbe: the add-ons
# that link it are added only once the "be" target exists, while the
# optional Haiku GPU drivers below do not need it.

option(VOS_WITH_HAIKU_GPU_DRIVERS
	"Build the Haiku GPU kernel drivers and accelerants (need a Haiku kernel)"
	OFF)

if(TARGET be)
	foreach(_addon_dir
			control_look
			decorators
			index_server
			input_server
			locale
			mail_daemon
			media
			network_settings
			neural
			opengl
			print
			quick_look
			screen_savers
			tracker
			translators)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_addon_dir}/CMakeLists.txt")
			add_subdirectory(${_addon_dir})
		endif()
	endforeach()
endif()

# Haiku GPU kernel drivers and their accelerants, sources untouched. The
# drivers need a Haiku kernel, so with this ON configuring works but the
# drivers do not compile on Linux. Accelerants that link libbe are skipped
# until the "be" target exists.
if(VOS_WITH_HAIKU_GPU_DRIVERS)
	message(STATUS "Haiku GPU drivers and accelerants: ON (need a Haiku kernel to compile)")
	include(${CMAKE_CURRENT_SOURCE_DIR}/HaikuGpu.cmake)
	add_subdirectory(kernel/drivers/graphics)
	add_subdirectory(accelerants)
endif()
