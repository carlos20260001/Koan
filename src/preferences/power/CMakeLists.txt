# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The Power preferences write ~/config/settings/system/power, which
# power_daemon (src/servers/power, on logind/UPower/power-profiles-daemon)
# reads.
Application(
	Power

	SOURCES
	Power.cpp

	LIBS localestub
	RDEF Power.rdef
)

DoCatalogs("x-vnd.vos-Power" preferences/power)
