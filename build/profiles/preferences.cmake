# Target names (cmake-unique): used for ImageInclude and DEPENDS.
# air/OS's preference panels; VitruvianOS's own panels (Accounts, Camera,
# Hardware, Location, User) are not part of this tree.
set(SYSTEM_PREFERENCES_TARGETS
	Appearance
	Backgrounds
	Bluetooth
	DataTranslations
	Deskbar_prefs
	E-mail
	FileTypes
	Input
	Joysticks
	Keymap
	Locale
	MCP
	Media
	Network
	Notifications
	Power
	Printers
	Screen
	ScreenSaver
	Shortcuts
	Sounds
	Time
	Tracker_prefs
)

# Installed binary names (OUTPUT_NAME where it differs): used for symlinks and staging
set(SYSTEM_PREFERENCES
	Appearance
	Backgrounds
	Bluetooth
	DataTranslations
	Deskbar
	E-mail
	FileTypes
	Input
	Joysticks
	Keymap
	Locale
	MCP
	Media
	Network
	Notifications
	Power
	Printers
	Screen
	ScreenSaver
	Shortcuts
	Sounds
	Time
	Tracker
)

VosKeepTargets(SYSTEM_PREFERENCES_TARGETS PARALLEL SYSTEM_PREFERENCES)
ImageInclude("/system/preferences" ${SYSTEM_PREFERENCES_TARGETS})
