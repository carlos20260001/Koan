# ported subset: upstream adds app, device, interface, net, shared, storage
# and support. Each one is added once its CMakeLists.txt exists.
foreach(_kit app device interface locale net shared storage support)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_kit}/CMakeLists.txt")
		add_subdirectory(${_kit})
	endif()
endforeach()
