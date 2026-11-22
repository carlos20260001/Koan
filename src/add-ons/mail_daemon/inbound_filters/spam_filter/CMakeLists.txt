# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(SpamFilter
	SOURCES
	SpamFilterConfig.cpp
	SpamFilter.cpp

	RDEF SpamFilter.rdef
)
DoCatalogs("x-vnd.vos-SpamFilter" add-ons/mail_daemon/inbound_filters/spam_filter)
