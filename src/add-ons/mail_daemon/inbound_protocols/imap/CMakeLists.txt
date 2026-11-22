# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(IMAP
	SOURCES
	IMAPProtocol.cpp
	ConfigView.cpp
	FolderConfigWindow.cpp
	IMAPFolder.cpp
	IMAPConnectionWorker.cpp
	IMAPMailbox.cpp
	Settings.cpp
	Utilities.cpp
	imap_lib/Commands.cpp
	imap_lib/Protocol.cpp
	imap_lib/Response.cpp

	LIBS shared

	RDEF IMAP.rdef
)
DoCatalogs("x-vnd.vos-IMAP" add-ons/mail_daemon/inbound_protocols/imap)
# -iquote: src/kits/tracker on the global include path has its own
# Commands.h.
target_compile_options(IMAP PRIVATE "-iquote${CMAKE_CURRENT_SOURCE_DIR}/imap_lib")
UsePrivateHeaders(IMAP libroot)
