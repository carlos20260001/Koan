set(CORE_APPLICATIONS
	Deskbar
	Tracker
)
VosKeepTargets(CORE_APPLICATIONS)
ImageInclude("/system/" ${CORE_APPLICATIONS})

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
	Camera
	Cortex
	DeskCalc
	DiskProbe
	DiskUsage
	DriveSetup
	Expander
	GLTeapot
	Installer
	LaunchBox
	LocalSend
	Magnify
	MediaConverter
	MediaPlayer
	NetworkStatus
	NetPlus
	PackageManager
	People
	Passwords
	PrintOMatic
	ResEdit
	SerialConnect
	#Screenshot
	ShowImage
	SoundRecorder
	StyledEdit
	Terminal
	#TextSearch
	WebMail
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

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Applications\")
")
foreach(app ${SYSTEM_APPS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Applications/${app}\"
			SYMBOLIC)
	")
endforeach()

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

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Demos\")
")
foreach(app ${DESKBAR_DEMOS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Demos/${app}\"
			SYMBOLIC)
	")
endforeach()

set(DESKBAR_APPLETS
	AudioMixer
	#AutoRaise
	BluetoothStatus
	Clock
	LocationStatus
	Messages
	MPRISReplicant
	NetworkStatus
	NetworkStatus
	NotificationReplicant
	OverlayImage
	Phone
	PhoneSignal
	PowerStatus
	ProcessController
	Pulse
)

VosKeepTargets(DESKBAR_APPLETS)
ImageInclude("/system/apps" ${DESKBAR_APPLETS})

install(CODE "
	file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/deskbar/menu/Desktop applets\")
")
foreach(app ${DESKBAR_APPLETS})
	install(CODE "
		file(CREATE_LINK \"/system/apps/${app}\"
			\"\$ENV{DESTDIR}/system/data/deskbar/menu/Desktop applets/${app}\"
			SYMBOLIC)
	")
endforeach()
