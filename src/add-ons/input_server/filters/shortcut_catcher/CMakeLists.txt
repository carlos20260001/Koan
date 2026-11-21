AddOn(
	shortcut_catcher
	SHARED

	LIBS
	be

	SOURCES
	ShortcutsServerFilter.cpp
	KeyCommandMap.cpp
	CommandExecutor.cpp
	CommandActuators.cpp
	BitFieldTesters.cpp
	KeyInfos.cpp
	ParseCommandLine.cpp
)

UsePrivateHeaders(shortcut_catcher input interface)