# package_daemon: air/OS's hpkg activation daemon (packagefs). Unused on
# VitruvianOS, whose packages are Debian packages installed by apt
# (libvpackage); it is built against libpackage (target "bpackage") so that
# the code keeps compiling, and nothing starts it.
Server(
	package_daemon

	SOURCES
	CommitTransactionHandler.cpp
	Constants.cpp
	DebugSupport.cpp
	Exception.cpp
	FSTransaction.cpp
	FSUtils.cpp
	Job.cpp
	JobQueue.cpp
	Package.cpp
	PackageDaemon.cpp
	PackageFile.cpp
	PackageFileManager.cpp
	PackageManager.cpp
	ProblemWindow.cpp
	ResultWindow.cpp
	Root.cpp
	Volume.cpp
	VolumeState.cpp

	LIBS localestub bpackage
	RDEF package_daemon.rdef
)

VosPrivateHeaders(package_daemon app interface shared storage system package)
DoCatalogs("x-vnd.vos-package_daemon" servers/package)
