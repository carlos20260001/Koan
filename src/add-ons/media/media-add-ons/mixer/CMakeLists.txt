# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The system audio mixer node: media_server asks media_addon_server for it
# (BMediaRoster::GetAudioMixer()).
MediaKitAddOn(mixer.media_addon
	SOURCES
	AudioMixer.cpp
	ByteSwap.cpp
	Interpolate.cpp
	MixerAddOn.cpp
	MixerCore.cpp
	MixerInput.cpp
	MixerOutput.cpp
	MixerSettings.cpp
	MixerUtils.cpp
	Resampler.cpp

	LIBS localestub
	RDEF AudioMixer.rdef
)
DoCatalogs("x-vnd.vos-mixer.media_addon" add-ons/media/media-add-ons/mixer)
