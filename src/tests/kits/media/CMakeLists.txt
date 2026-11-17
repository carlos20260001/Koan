# Media Kit tests that build on Linux. They need a running media_server and
# media_addon_server (Nexus), and media files or audio hardware, so they are
# built, not registered with CTest. The CppUnit suite (libmediatest:
# AreaTest, BufferTest, TimeCodeTest, ...) waits for cppunit, and the
# subdirectories (decoder/reader tests, nodetest, playsound, ...) are not
# ported yet; the Jamfile stays for them.
#
# The CTest that does run, media_symbol_versions, is declared next to the
# kit in src/kits/media/CMakeLists.txt.

# media_track_times_test <file>: start_time of decoded frames is in
# microseconds.
Test(media_track_times_test
	SOURCES media_track_times_test.cpp
	LIBS media
)

# buffers of killed teams in the shared buffer list: run "hold 64" several
# times, kill each with -9, then "check"
Test(buffer_group_leak_test
	SOURCES buffer_group_leak_test.cpp
	LIBS media
)

Test(mixer_monitor_test
	SOURCES mixer_monitor_test.cpp
	LIBS media
)

Test(VideoDecoder
	SOURCES VideoDecoder.cpp
	LIBS media
)

foreach(_test media_track_times_test buffer_group_leak_test
		mixer_monitor_test VideoDecoder)
	UseMediaKit(${_test})
endforeach()
