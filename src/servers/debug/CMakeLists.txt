# debug_server: air/OS's crash dialog. On Haiku it is the kernel's default
# debugger (DebugServer.cpp: debug nub messages, gdb/Debugger hand-over),
# which has no Linux counterpart; that file needs Haiku's debug kernel
# interface and is not built here. On Linux, CoredumpServer.cpp follows
# systemd-coredump's journal entries for the user's crashed programs and
# shows the same dialog (DebugWindow.cpp), saving reports and core files
# with coredumpctl.
# TODO(linux backend): catch the crash before the program is gone (a
# ptrace-based handler, as the debug kit port behind VOS_WITH_DEBUG_KIT
# would allow) to offer air/OS's Debugger.
Server(
	debug_server

	SOURCES
	CoredumpServer.cpp
	DebugWindow.cpp

	LIBS shared localestub systemd
	RDEF debug_server.rdef
)

VosPrivateHeaders(debug_server app debug shared storage)
DoCatalogs("x-vnd.vos-debug_server" servers/debug)
