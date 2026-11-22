# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(POP3
	SOURCES
	ConfigView.cpp
	MessageIO.cpp
	POP3.cpp
	md5c.c

	RDEF POP3.rdef
)
DoCatalogs("x-vnd.vos-POP3" add-ons/mail_daemon/inbound_protocols/pop3)
