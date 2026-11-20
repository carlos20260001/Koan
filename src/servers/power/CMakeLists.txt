# power_daemon: air/OS's power policy daemon (battery and AC profiles, the
# display on battery, lid action, clamshell mode, power key). On Linux the
# events and actions are systemd-logind's, UPower's and
# power-profiles-daemon's over the system D-Bus (linux_power.cpp); the
# Haiku monitors stay and find no devices.
Server(
	power_daemon

	SOURCES
	ac_monitor.cpp
	lid_monitor.cpp
	power_daemon.cpp
	power_button_monitor.cpp
	power_policy.cpp
	thermal_trip_monitor.cpp
	linux_power.cpp

	LIBS systemd

	RDEF power_daemon.rdef
)

VosPrivateHeaders(power_daemon app interface kernel storage)
