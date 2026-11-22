# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# yuvrgb.nasm was not built by the Jamfile either.
MediaKitAddOn(video_mixer.media_addon
	SOURCES
	VideoMixerNode.cpp
	VideoMixerNodeConsumer.cpp
	VideoMixerNodeProducer.cpp
	VideoMixerNodeEventLooper.cpp
	VideoMixerAddOn.cpp
	CpuCapabilities.cpp
	BufferMixer.cpp
)
