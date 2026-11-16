AddOn(midi
	SHARED

	SOURCES
	Midi.cpp
	MidiPort.cpp
	MidiStore.cpp
	MidiSynth.cpp
	MidiSynthFile.cpp
	MidiSettings.cpp
	MidiText.cpp
	Samples.cpp
	Synth.cpp

	LIBS be root media2 midi2
)

UsePrivateHeaders(midi support media2 midi2)

target_include_directories(midi PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	# air/OS's MidiSettings (read/write_midi_settings, used by the Media
	# preferences); UsePrivateHeaders has no "midi" entry.
	${CMAKE_SOURCE_DIR}/headers/private/midi
)
