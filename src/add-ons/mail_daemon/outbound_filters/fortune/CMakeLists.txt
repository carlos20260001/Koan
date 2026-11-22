# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(Fortune
	SOURCES
	ConfigView.cpp
	FortuneFilter.cpp

	RDEF Fortune.rdef
)
DoCatalogs("x-vnd.vos-Fortune" add-ons/mail_daemon/outbound_filters/fortune)
