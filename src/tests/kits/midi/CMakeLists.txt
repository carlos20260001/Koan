# MIDI kit tests. Both need input (a MIDI file, a BeOS synth file) and a
# running PipeWire for sound, so they are built, not registered with CTest.

# miditest1 <file.mid>: plays a file through BMidiStore into BMidiText.
Test(miditest1
	SOURCES test1.cpp
	LIBS midi
)

# sfr <synth file>: reads and plays the instruments of a BeOS synth file.
Test(sfr
	SOURCES
	synth_file_reader/SynthFileReader.cpp
	synth_file_reader/SynthFile.cpp
	synth_file_reader/main.cpp
	LIBS game
)
target_include_directories(sfr PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/synth_file_reader)
UsePrivateHeaders(sfr shared)
