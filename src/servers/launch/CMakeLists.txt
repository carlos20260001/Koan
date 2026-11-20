# air/OS's launch_daemon (Haiku's, with air/OS changes). VitruvianOS has no
# launch_daemon: systemd starts janus, janus answers the launch daemon port
# for the system daemons and janus_session does it for a login's servers,
# reading no data/launch files. launch_daemon is kept building so its code
# stays usable (and its unit tests run), but it is not installed or started:
# two answers to B_LAUNCH_DAEMON_PORT_NAME would race (PORTING.md M9-W3).
Server(
	launch_daemon

	SOURCES
	LaunchDaemon.cpp

	BaseJob.cpp
	Conditions.cpp
	Events.cpp
	FileWatcher.cpp
	Job.cpp
	Log.cpp
	NetworkWatcher.cpp
	SettingsParser.cpp
	Target.cpp
	Utility.cpp
	VolumeWatcher.cpp
	Worker.cpp

	# init jobs
	AbstractEmptyDirectoryJob.cpp
	InitRealTimeClockJob.cpp
	InitSharedMemoryDirectoryJob.cpp
	InitTemporaryDirectoryJob.cpp

	# libmultiuser_utils.a in the Jam build
	../../bin/multiuser/multiuser_utils.cpp

	INCLUDES
	"${CMAKE_SOURCE_DIR}/src/bin/multiuser"
	LIBS shared crypt
	RDEF LaunchDaemon.rdef
)

UsePrivateHeaders(launch_daemon app libroot shared storage support kernel
	system)
