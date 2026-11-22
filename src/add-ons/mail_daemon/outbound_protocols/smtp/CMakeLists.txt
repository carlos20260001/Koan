# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(SMTP
	SOURCES
	ConfigView.cpp
	SMTP.cpp
	md5c.c

	RDEF SMTP.rdef
)
DoCatalogs("x-vnd.vos-SMTP" add-ons/mail_daemon/outbound_protocols/smtp)
