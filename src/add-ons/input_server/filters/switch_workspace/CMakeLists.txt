AddOn(
	switch_workspace
	SHARED

	LIBS
	be

	SOURCES
	SwitchWorkspaceInputFilter.cpp
)

UsePrivateHeaders(switch_workspace input app interface)