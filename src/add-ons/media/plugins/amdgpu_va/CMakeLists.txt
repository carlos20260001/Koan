# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Hardware video decoding through VA-API (Debian's libva and the system
# FFmpeg, whose libavutil has the VA-API hwcontext), installed as "amduvd".
# air/OS linked a private libavcodec statically because its system FFmpeg had
# no VA-API; Debian's has, so the shared libraries are used.
find_package(PkgConfig REQUIRED)
pkg_check_modules(AMDUVD REQUIRED libva libavcodec libavutil)

MediaKitAddOn(amduvd
	SOURCES
	AmdgpuVaPlugin.cpp
	H264Output.cpp
	HevcOutput.cpp

	LIBS ${AMDUVD_LIBRARIES}
)
target_include_directories(amduvd PRIVATE ${AMDUVD_INCLUDE_DIRS})
target_compile_options(amduvd PRIVATE ${AMDUVD_CFLAGS_OTHER})
