# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Cortex's example media add-ons (classic Media Kit), loaded by
# media_addon_server from /system/add-ons/media. NullFilter was never
# built by air/OS's Jamfiles and stays unbuilt.

# CortexMediaAddOn(<name> <source>...)
function(CortexMediaAddOn name)
	AddOn(${name} SHARED SOURCES ${ARGN} LIBS media localestub be root)
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_include_directories(${name} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/common
		${CMAKE_CURRENT_SOURCE_DIR}/audioOps)
	target_link_options(${name} PRIVATE -Wl,--no-undefined)
	UseMediaKit(${name})
endfunction()

CortexMediaAddOn(cortex_audioadapter.media_addon
	AudioAdapter/AudioAdapterAddOn.cpp
	AudioAdapter/AudioAdapterNode.cpp
	AudioAdapter/AudioAdapterParams.cpp
	audioOps/AudioAdapterOp.cpp
	common/AudioBuffer.cpp
	common/AudioFilterNode.cpp
	common/IParameterSet.cpp
	common/RawBuffer.cpp
	common/SoundUtils.cpp
)
target_include_directories(cortex_audioadapter.media_addon PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/AudioAdapter)

CortexMediaAddOn(cortex_flanger.media_addon
	Flanger/FlangerApp.cpp
	Flanger/FlangerAddOn.cpp
	Flanger/FlangerNode.cpp
	common/AudioBuffer.cpp
	common/MediaNodeControlApp.cpp
	common/RawBuffer.cpp
	common/SoundUtils.cpp
)
target_include_directories(cortex_flanger.media_addon PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Flanger)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Flanger)
CompileRdef(cortex_flanger.media_addon Flanger/Resource.rdef)
LinkRdefs(cortex_flanger.media_addon)

CortexMediaAddOn(cortex_logging_consumer.media_addon
	LoggingConsumer/LoggingConsumerApp.cpp
	LoggingConsumer/LoggingConsumerAddOn.cpp
	LoggingConsumer/NodeHarnessApp.cpp
	LoggingConsumer/NodeHarnessWin.cpp
	LoggingConsumer/LogWriter.cpp
	LoggingConsumer/LoggingConsumer.cpp
	common/MediaNodeControlApp.cpp
)
target_include_directories(cortex_logging_consumer.media_addon PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/LoggingConsumer)

DoCatalogs("x-vnd.moon-AudioAdapter.media_addon" apps/cortex/addons/AudioAdapter)
DoCatalogs("x-vnd.moon-Flanger.media_addon" apps/cortex/addons/Flanger)
DoCatalogs("x-vnd.Be.LoggingConsumerApp" apps/cortex/addons/LoggingConsumer)
