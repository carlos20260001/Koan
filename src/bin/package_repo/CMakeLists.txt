# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(package_repo
	SOURCES
	command_create.cpp
	command_list.cpp
	command_update.cpp
	package_repo.cpp

	LIBS bpackage
	INCLUDES ${CMAKE_SOURCE_DIR}/src/bin/package
)
UsePrivateHeaders(package_repo shared)
