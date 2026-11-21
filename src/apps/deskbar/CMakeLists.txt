Application(
	Deskbar

	SOURCES
	BarApp.cpp
	BarMenuBar.cpp
	BarMenuTitle.cpp
	BarView.cpp
	BarWindow.cpp
	CalendarMenuWindow.cpp
	DeskbarMenu.cpp
	DeskbarUtils.cpp
	ExpandoMenuBar.cpp
	InlineScrollView.cpp
	LocationMenuItem.cpp
	PreferencesWindow.cpp
	ScreenCornerSelector.cpp
	ShowHideMenuItem.cpp
	StatusView.cpp
	StatusViewShelf.cpp
	TeamMenu.cpp
	TeamMenuItem.cpp
	TimeView.cpp
	TruncatableMenuItem.cpp
	WindowMenu.cpp
	WindowIcons.cpp
	WindowMenuItem.cpp
	ResourceSet.cpp
	Switcher.cpp

	LIBS tracker shared localestub
	RDEF Deskbar.rdef icons.rdef
)

target_compile_definitions(Deskbar PRIVATE
	DB_ADDONS
	_INCLUDES_CLASS_DEVICE_MAP=1
	_SUPPORTS_RESOURCES=1
	_SUPPORTS_FEATURE_SCRIPTING=1
	_SHOW_CALENDAR_MENU_WINDOW=1
	MOUNT_MENU_IN_DESKBAR=1
)

UsePrivateHeaders(Deskbar app interface locale shared tracker)
# Tracker's own headers (Utilities.h, NavMenu.h, ...) live beside its sources.
target_include_directories(Deskbar PRIVATE
	${PROJECT_SOURCE_DIR}/src/kits/tracker
)

DoCatalogs("x-vnd.Be-TSKB" apps/deskbar)
