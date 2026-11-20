# midi_server: air/OS's MIDI server for Haiku's /dev/midi drivers.
# Unused by default on VitruvianOS: libmidi2 is a PipeWire MIDI client and
# needs no server, and Linux MIDI devices are ALSA sequencer ports that
# PipeWire already publishes. It is built so that the code keeps compiling
# and can take an ALSA backend later; nothing starts it.
Server(
	midi_server

	SOURCES
	MidiServerApp.cpp
	DeviceWatcher.cpp
	PortDrivers.cpp

	LIBS midi2 shared localestub
	RDEF midi_server.rdef
)

VosPrivateHeaders(midi_server app interface locale midi midi2 shared storage)
DoCatalogs("x-vnd.vos-midi_server" servers/midi)
