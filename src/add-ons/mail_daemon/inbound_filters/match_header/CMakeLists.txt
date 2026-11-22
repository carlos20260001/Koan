# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MailDaemonAddOn(MatchHeader
	SOURCES
	ConfigView.cpp
	RuleFilter.cpp
	StringMatcher.cpp

	RDEF MatchHeader.rdef
)
DoCatalogs("x-vnd.vos-MatchHeader" add-ons/mail_daemon/inbound_filters/match_header)
