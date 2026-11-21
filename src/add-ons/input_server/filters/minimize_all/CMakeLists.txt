AddOn(
	minimize_all
	SHARED

	LIBS
	be tracker

	SOURCES
	MinimizeAllInputFilter.cpp
)

UsePrivateHeaders(minimize_all input interface tracker)