# mail_daemon: air/OS's mail daemon (libmail).
Server(
	mail_daemon

	SOURCES
	DeskbarView.cpp
	ErrorLogWindow.cpp
	LEDAnimation.cpp
	MailDaemonApplication.cpp
	main.cpp
	DefaultNotifier.cpp

	LIBS mail tracker localestub
	RDEF mail_daemon.rdef DeskbarViewIcons.rdef
)

VosPrivateHeaders(mail_daemon app mail shared tracker)
target_include_directories(mail_daemon PRIVATE
	"${PROJECT_SOURCE_DIR}/headers/os/mail"
	"${PROJECT_SOURCE_DIR}/headers/os/add-ons/mail_daemon"
	"${PROJECT_SOURCE_DIR}/src/kits/tracker")
DoCatalogs("x-vnd.Be-POST" servers/mail)
