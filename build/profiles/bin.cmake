	set(BIN_DIRECTORY
	addattr
	keystore
	linkcatkeys
	dumpcatalog
	media_client
	pc
	query
	rc
	jam
	catarea
	error
	chop
	driveinfo
	finddir
	get_driver_settings
	fortune_vos
	listarea
	listimage
	listport
	listsem
	lsindex
	prio
	rmattr
	rmindex
	system_time
	unchop
	catattr
	clipboard
	diskimage
	dpms
	draggers
	ffm
	iroster
	isvolume
	listattr
	listfont
	listres
	message
	mkindex
	launch_roster
	modifiers
	open
	mvattr
	quit
	roster
	trash
	version
	waitfor
	WindowShade
	alert
	dstcheck
	hey
	reindex
	resattr
	screeninfo
	setcontrollook
	setdecor
	settype
	spybmessage
	urlwrapper
	checkitout
	copyattr
	diff_zip
	mimeset
	mountvolume
	setmime
	seticon
	setapptype
	sysinfo
	xres
	notify
	translate
	filepanel
	vos-signature-migrate
	xclientsig
	screen_blanker
	# screenshot_cli: apps.cmake installs the command line screenshot tool
	# (target screenshot) to /system/bin.
)
VosKeepTargets(BIN_DIRECTORY)
ImageInclude("/bin" ${BIN_DIRECTORY})

# The other commands of src/bin this tree ports (PORTING.md, src/bin
# table). Targets whose name is taken by a kit or by CPack carry a _cli
# suffix and keep the command name as OUTPUT_NAME. Built but not
# installed: locale (Debian's libc-bin owns it) and media_client (a test
# client of the classic Media Kit, a test in the Jamfile too).
set(BIN_COMMANDS
	beep
	bfsinfo
	bfswhich
	bt_a2dp
	bt_dev_info
	bt_discovery
	bt_le
	chkindex
	collectcatkeys
	desklink
	findpaths
	hd
	installsound
	keymap_cli
	listdev
	listusb
	mail_cli
	mail2mbox
	mbox2mail
	mediakey
	neural_cli
	package_repo
	recover
	release
	screenmode
	setusbconfig
	setversion
	setvolume
	spamdbm
	wakeonlan
)
VosKeepTargets(BIN_COMMANDS)
ImageInclude("/bin" ${BIN_COMMANDS})

# The hpkg tool: "package" is CPack's target name.
if(TARGET package_cli)
	install(PROGRAMS $<TARGET_FILE:package_cli> DESTINATION /bin
		RENAME package COMPONENT runtime)
endif()
