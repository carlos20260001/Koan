# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	Time

	SOURCES
	AnalogClock.cpp
	BaseView.cpp
	ClockView.cpp
	DateTimeView.cpp
	NetworkTimeView.cpp
	ntp.cpp
	Time.cpp
	TimeSettings.cpp
	TimeWindow.cpp
	TimeZoneListItem.cpp
	TimeZoneListView.cpp
	TimeBackend.cpp
	TZDisplay.cpp
	ZoneView.cpp

	LIBS shared localestub
	RDEF Time.rdef
)

# ICU's headers need C++17.
target_compile_options(Time PRIVATE -std=c++17)
UsePrivateHeaders(Time app interface locale shared system)
DoCatalogs("x-vnd.vos-Time" preferences/time)
