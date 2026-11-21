set(CORE_APPLICATIONS
	Deskbar
	Tracker
)
VosKeepTargets(CORE_APPLICATIONS)
ImageInclude("/system/" ${CORE_APPLICATIONS})

# Tracker's mail add-ons. The other Tracker add-ons are listed in
# base.cmake (TRACKER_ADDONS); these link air/OS's mail kit and are kept
# only when it builds. Tracker reads their resources from the file, so
# they need no entry in apps_attrs.
set(TRACKER_MAIL_ADDONS
	LabelAs
	MarkAs
	MarkAsRead
)
VosKeepTargets(TRACKER_MAIL_ADDONS)
ImageInclude("/system/add-ons/Tracker" ${TRACKER_MAIL_ADDONS})

# Greeter binary — /system/servers/ so janus finds it by name.
if(TARGET vitruvian-login) # ported subset: not in this tree yet
ImageInclude("/system/servers" vitruvian-login)
endif()

# Polkit authentication agent (auto-started with the graphical session).
if(TARGET vos-polkit-agent) # ported subset: not in this tree yet
ImageInclude("/system/servers" vos-polkit-agent)
endif()

set(SYSTEM_APPS
	AboutSystem
	ActivityMonitor
	BootManager
	Camera
	Cortex
	DebugAnalyzer
	DeskCalc
	DiskProbe
	DiskUsage
	DriveSetup
	Expander
	GLTeapot
	Installer
	LaunchBox
	LegacyPackageInstaller
	LocalSend
	Magnify
	MediaConverter
	MediaPlayer
	MusicCollection
	NVPlay
	NetworkStatus
	NetPlus
	PackageManager
	People
	Passwords
	PrintOMatic
	ResEdit
	ScreenReader
	SerialConnect
	#Screenshot
	ShowImage
	SoundRecorder
	StyledEdit
	Switcher
	Terminal
	#TextSearch
	TV
	WebMail
	WebPositive
	Workspaces
)
VosKeepTargets(SYSTEM_APPS)
ImageInclude("/system/apps" ${SYSTEM_APPS})

# Installed but not in the Deskbar menu: started by the system, not users.
set(SYSTEM_APPS_UNLISTED
	FirstBootPrompt
	# TryNow popup: launched by /system/boot/first_login/10-expand-volume.
	ExpandVolume
)
VosKeepTargets(SYSTEM_APPS_UNLISTED)
ImageInclude("/system/apps" ${SYSTEM_APPS_UNLISTED})


set(DESKBAR_DEMOS
	#FontDemo
	Gradients
	Mandelbrot
	Pairs
	PrivilegedGuy
	Sudoku
)

set(DESKBAR_DEMOS_TARGETS
	Gradients
	Mandelbrot
	Pairs
	PrivilegedGuy
	Sudoku
)

VosKeepTargets(DESKBAR_DEMOS_TARGETS)
VosKeepTargets(DESKBAR_DEMOS)
ImageInclude("/system/apps" ${DESKBAR_DEMOS_TARGETS})


set(DESKBAR_APPLETS
	AudioMixer
	AutoRaise
	BluetoothStatus
	Clock
	LocationStatus
	Messages
	MPRISReplicant
	NetworkStatus
	NotificationReplicant
	OverlayImage
	Phone
	PhoneSignal
	PowerStatus
	ProcessController
	Pulse
	WiFiStatus
)

VosKeepTargets(DESKBAR_APPLETS)
ImageInclude("/system/apps" ${DESKBAR_APPLETS})


# Cortex (demos port): the add-on host Cortex starts by signature, and its
# classic Media Kit add-ons, where air/OS installed them (SYSTEM_BIN and
# SYSTEM_ADD_ONS_MEDIA).
set(CORTEX_SYSTEM_BIN CortexAddOnHost)
VosKeepTargets(CORTEX_SYSTEM_BIN)
ImageInclude("/system/bin" ${CORTEX_SYSTEM_BIN})
set(CORTEX_MEDIA_ADDONS
	cortex_audioadapter.media_addon
	cortex_flanger.media_addon
	cortex_logging_consumer.media_addon
)
VosKeepTargets(CORTEX_MEDIA_ADDONS)
ImageInclude("/system/add-ons/media" ${CORTEX_MEDIA_ADDONS})
