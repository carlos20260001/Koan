# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# BFS inspection and recovery tools. They read a BFS volume through a
# device or image file with plain file I/O, so they need no file system
# driver.
add_library(bfs_tools_lib STATIC
	lib/bfs.cpp
	lib/Bitmap.cpp
	lib/BPlusTree.cpp
	lib/Disk.cpp
	lib/dump.cpp
	lib/Hashtable.cpp
	lib/Inode.cpp
)
target_include_directories(bfs_tools_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set_target_properties(bfs_tools_lib PROPERTIES
	COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
target_link_libraries(bfs_tools_lib PUBLIC root be)
UsePrivateHeaders(bfs_tools_lib shared)

foreach(_tool bfsinfo chkindex bfswhich recover)
	Application(${_tool} SOURCES ${_tool}.cpp LIBS bfs_tools_lib shared)
	UsePrivateHeaders(${_tool} shared)
endforeach()
