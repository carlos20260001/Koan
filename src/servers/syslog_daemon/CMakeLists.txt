# syslog_daemon: air/OS's system logger. On Linux, syslog() and the kernel
# log go to the systemd journal, so the daemon reads the journal
# (SyslogDaemon.cpp) instead of a kernel port, and hands each message to its
# handlers as on air/OS: the rotated, time-stamped log file and the live
# listeners (SYSLOG_ADD_LISTENER).
Server(
	syslog_daemon

	SOURCES
	SyslogDaemon.cpp
	syslog_output.cpp
	listener_output.cpp

	LIBS localestub systemd
	RDEF SyslogDaemon.rdef
)

VosPrivateHeaders(syslog_daemon app syslog_daemon system)
DoCatalogs("x-vnd.vos-SystemLogger" servers/syslog_daemon)
