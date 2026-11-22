# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(NewMailNotification
	SOURCES
	NotifierFilter.cpp
	NotifierConfigView.cpp

	RDEF NotifierFilter.rdef
)
DoCatalogs("x-vnd.vos-NewMailNotification" add-ons/mail_daemon/inbound_filters/notifier)
