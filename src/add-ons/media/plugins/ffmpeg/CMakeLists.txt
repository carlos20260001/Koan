# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The FFmpeg plugin: readers, decoders, writers and encoders for the classic
# Media Kit, on Debian's FFmpeg (libavcodec61 and companions, FFMPEG in
# build/deps.cmake). The hand-written MMX/SSE converters (gfx_conv_mmx.cpp,
# yuvrgb_*.nasm) were not built by the Jamfile either; libswscale converts.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG_PLUGIN REQUIRED
	libavcodec libavfilter libavformat libavutil libswscale libswresample)

MediaKitAddOn(ffmpeg
	SOURCES
	AVCodecDecoder.cpp
	AVCodecEncoder.cpp
	AVFormatReader.cpp
	AVFormatWriter.cpp
	CodecTable.cpp
	DemuxerTable.cpp
	EncoderTable.cpp
	FFmpegPlugin.cpp
	MuxerTable.cpp
	CpuCapabilities.cpp
	gfx_conv_c.cpp
	gfx_conv_c_lookup.cpp
	gfx_util.cpp

	LIBS ${FFMPEG_PLUGIN_LIBRARIES}
)

# The sources include "avcodec.h", "swscale.h", ... without the library
# directory, as the Jamfile's UseHeaders did. -iquote, not -I: libavutil has
# a time.h and an error.h that must not shadow the C library's.
foreach(_lib libavcodec libavfilter libavformat libavutil libswscale
		libswresample)
	target_compile_options(ffmpeg PRIVATE
		"-iquote${HEADERS_PATH_BASE}/${VITRUVIAN_MULTIARCH_TRIPLE}/${_lib}")
endforeach()
target_include_directories(ffmpeg PRIVATE ${FFMPEG_PLUGIN_INCLUDE_DIRS})
target_compile_definitions(ffmpeg PRIVATE __STDC_CONSTANT_MACROS)
target_compile_options(ffmpeg PRIVATE -Wno-deprecated-declarations)
