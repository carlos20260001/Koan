# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's MediaConverter. The engine (engine/) reads, decodes, filters and
# writes with FFmpeg; the encoders are the GPUs' through VA-API (libva,
# opened at run time on /dev/dri) and NVENC, the SoCs' (Qualcomm Iris,
# Allwinner Cedar, Rockchip MPP) and on the CPU x264, x265 and SVT-AV1
# (opened at run time) and FFmpeg's own. The classic Media Kit plays the
# preview through media_server.
foreach(_lib media tracker translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "MediaConverter: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(VOS_FFMPEG IMPORTED_TARGET libavformat libavcodec
		libavfilter libavutil libswscale libswresample)
endif()
if(NOT VOS_FFMPEG_FOUND)
	message(STATUS "MediaConverter: skipped, needs FFmpeg (libavformat-dev, "
		"libavfilter-dev, libswscale-dev, libswresample-dev)")
	return()
endif()

set(_mc_qcom ${PROJECT_SOURCE_DIR}/src/add-ons/media/plugins/qcom_video)
set(_mc_sunxi ${PROJECT_SOURCE_DIR}/src/add-ons/media/encoders/sunxi_venc)

add_library(mediaconverter_engine STATIC
	engine/BitWriter.cpp
	${_mc_sunxi}/CedarEncoder.cpp
	engine/CedarVideoEncoder.cpp
	engine/DiscSource.cpp
	${_mc_qcom}/IrisDevice.cpp
	${_mc_qcom}/IrisEncoder.cpp
	${_mc_qcom}/IrisSession.cpp
	engine/Formats.cpp
	engine/HardwareCaps.cpp
	engine/IrisVideoEncoder.cpp
	engine/LavcVideoEncoder.cpp
	engine/MediaProbe.cpp
	engine/MppVideoEncoder.cpp
	engine/NvencEncoder.cpp
	engine/OutputSettings.cpp
	engine/Presets.cpp
	engine/SoftwareEncoders.cpp
	engine/Transcoder.cpp
	engine/VaapiEncoder.cpp
	engine/VaapiLibrary.cpp
	engine/VideoEncoder.cpp
)
target_compile_options(mediaconverter_engine PRIVATE
	-include LinuxBuildCompatibility.h)
target_link_libraries(mediaconverter_engine PUBLIC
	be root PkgConfig::VOS_FFMPEG ${CMAKE_DL_LIBS})

foreach(_target mediaconverter_engine)
	target_compile_definitions(${_target} PRIVATE __STDC_CONSTANT_MACROS)
	UsePrivateHeaders(${_target} interface shared system)
	UseMediaKit(${_target})
	target_include_directories(${_target} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/engine
		${CMAKE_CURRENT_SOURCE_DIR}/app
		${_mc_qcom}
		${_mc_sunxi}
		${PROJECT_SOURCE_DIR}/headers/private/drivers
	)
	target_include_directories(${_target} SYSTEM PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
endforeach()

Application(
	MediaConverter

	SOURCES
	app/AudioPreview.cpp
	app/HardwareWindow.cpp
	app/JobProcess.cpp
	app/MediaConverterApp.cpp
	app/MediaConverterWindow.cpp
	app/OutputView.cpp
	app/QueueView.cpp
	app/SourceView.cpp

	# airTime's controller (app/airtime/README.md)
	app/airtime/ControlBar.cpp
	app/airtime/Look.cpp

	LIBS mediaconverter_engine media tracker translation localestub
	RDEF MediaConverter.rdef
)

# The engine from a shell, for tests (not part of the image).
Application(
	mediaconvert

	SOURCES
	cli/mediaconvert.cpp

	LIBS mediaconverter_engine
)

foreach(_target MediaConverter mediaconvert)
	target_compile_definitions(${_target} PRIVATE __STDC_CONSTANT_MACROS)
	UsePrivateHeaders(${_target} interface shared system)
	UseMediaKit(${_target})
	target_include_directories(${_target} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/engine
		${CMAKE_CURRENT_SOURCE_DIR}/app
		${CMAKE_CURRENT_SOURCE_DIR}/app/airtime
		${PROJECT_SOURCE_DIR}/headers/private/drivers
	)
	target_include_directories(${_target} SYSTEM PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
endforeach()

DoCatalogs("x-vnd.vos-MediaConverter" apps/mediaconverter)
