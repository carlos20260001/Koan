# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# T9 (predictive keypad) input method.
AddOn(
	t9
	SHARED

	LIBS
	be textencoding

	SOURCES
	T9InputServerMethod.cpp
)

UsePrivateHeaders(t9 interface)
