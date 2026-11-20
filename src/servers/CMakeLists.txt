# Servers. Each subdirectory is added only once its CMakeLists.txt exists;
# the others keep their Jamfiles until their wave ports them (PORTING.md M9).

# Adds headers/private/<name> for each name to <target>: UsePrivateHeaders()
# knows only the directories VitruvianOS has, not air/OS's (media, midi,
# bluetooth, osd, quicklook, ...).
function(VosPrivateHeaders target)
	UsePrivateHeaders(${target} ${ARGN})
	foreach(_dir IN LISTS ARGN)
		target_include_directories(${target} PRIVATE
			"${PROJECT_SOURCE_DIR}/headers/private/${_dir}")
	endforeach()
endfunction()

foreach(_server
		accessibility
		bluetooth
		debug
		index
		location
		mail
		mcp
		mdns
		media
		media_addon
		media_controls
		midi
		net
		osd
		package
		power
		print
		print_addon
		quick_look
		syslog_daemon)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_server}/CMakeLists.txt")
		add_subdirectory(${_server})
	endif()
endforeach()
