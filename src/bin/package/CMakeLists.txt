# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# hpkg packaging tool on the classic package kit (target bpackage).
Application(package_cli
	SOURCES
	command_add.cpp
	command_checksum.cpp
	command_create.cpp
	command_dump.cpp
	command_extract.cpp
	command_info.cpp
	command_list.cpp
	command_recompress.cpp
	package.cpp
	PackageWriterListener.cpp
	PackageWritingUtils.cpp

	LIBS bpackage
)
UsePrivateHeaders(package_cli libroot shared storage support)
