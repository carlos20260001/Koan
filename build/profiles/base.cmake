include(${CMAKE_CURRENT_LIST_DIR}/apps.cmake)


set(SYSTEM_SERVERS
	janus
	janus_session
	janus_launch
	app_server
	input_server
	notification_server
	mount_server
	registrar
	keystore_server
	notifications_bridge
)
VosKeepTargets(SYSTEM_SERVERS)
ImageInclude("/system/servers" ${SYSTEM_SERVERS})
# ported subset: notification_server is not in this tree yet.
if(EXISTS "${CMAKE_SOURCE_DIR}/src/servers/notification/org.freedesktop.Notifications.service")
ImageIncludeFile("${CMAKE_SOURCE_DIR}/src/servers/notification/org.freedesktop.Notifications.service" "/usr/share/dbus-1/services")
endif()


set(INPUT_SERVER_ADDONS
	keyboard
	mouse
)
VosKeepTargets(INPUT_SERVER_ADDONS)
ImageInclude("/system/add-ons/input_server/devices" ${INPUT_SERVER_ADDONS})
# virtualkeyboard builds but stays out: it opens its window every time
# input_server starts (its TODO.org), and air/OS does not ship it either.
# The input methods pen and t9 build too; air/OS ships an empty
# add-ons/input_server/methods, and the method menu's Deskbar replicant
# cannot load from this tree's input_server (src/servers/input), so they
# stay out as well.


# Every filter runs on every input message, so this list is hot-path.
# shortcut_catcher still stays out until its feature is wired up.
set(INPUT_SERVER_FILTERS
	accessibility_keys
	padblocker
	switch_workspace
	minimize_all
	screen_saver
	xvitruvian
#	shortcut_catcher
)
VosKeepTargets(INPUT_SERVER_FILTERS)
ImageInclude("/system/add-ons/input_server/filters" ${INPUT_SERVER_FILTERS})


# OpenGL is Mesa hgl (src/add-ons/opengl): libGL-hgl gives BGLView and the
# renderer roster, and air/OS's Zink add-on is the renderer; both are
# installed by the vos-mesa-hgl package, not from here (PORTING.md, M10 -
# air/OS Mesa (hgl + Zink)).


# Account provider add-ons, enumerated by BAccountRoster.
set(ACCOUNT_PROVIDER_ADDONS
	ImapProvider
	Oauth2Provider
)
VosKeepTargets(ACCOUNT_PROVIDER_ADDONS)
ImageInclude("/system/add-ons/accounts" ${ACCOUNT_PROVIDER_ADDONS})


set(SYSTEM_LIBS
	root
	be
	web
	game
	hwaccel
	media2
	printcups
	libscan
	prefs
	screensaver
	textencoding
	tracker
	translation
	textencoding
	shared
	vpackage
	localestub
	midi
	midi2
	outputaddon
	vos_x_client
)
VosKeepTargets(SYSTEM_LIBS)
ImageInclude("/lib" ${SYSTEM_LIBS})


set(SYSTEM_TRANSLATORS
	AVIFTranslator
	BMPTranslator
	GIFTranslator
	HVIFTranslator
	ICNSTranslator
	ICOTranslator
	JPEGTranslator
#	JPEG2000Translator: needs libjasper, which Debian 13 does not ship;
#	left out until ported to OpenJPEG (src/add-ons/translators/CMakeLists.txt)
	PCXTranslator
	PNGTranslator
	PPMTranslator
	PSDTranslator
	RAWTranslator
	RTFTranslator
	SGITranslator
	STXTTranslator
	TGATranslator
	TIFFTranslator
	WEBPTranslator
	WonderbrushTranslator
)
VosKeepTargets(SYSTEM_TRANSLATORS)
ImageInclude("/system/add-ons/Translators" ${SYSTEM_TRANSLATORS})


set(TRACKER_ADDONS
	FileType
	IconVader
	OpenTargetFolder
	OpenTerminal
	SendToBluetooth
	ZipOMatic
#	mark_as: needs libmail (not ported); skipped
)
VosKeepTargets(TRACKER_ADDONS)
ImageInclude("/system/add-ons/Tracker" ${TRACKER_ADDONS})

set(CATALOG_ADDONS
	plaintext
)
VosKeepTargets(CATALOG_ADDONS)
ImageInclude("/system/add-ons/locale/catalogs" ${CATALOG_ADDONS})

# media2 units and the LV2/LADSPA plugin adapter, and the classic Media
# Kit node add-ons (src/add-ons/media/media-add-ons; pipewire.media_addon
# installs itself).
set(MEDIA_ADDONS
	equalizer
	plugin_adapter
	equalizer.media_addon
	esound_sink.media_addon
	mixer.media_addon
	reader.media_addon
	tone_producer_demo.media_addon
	video_mixer.media_addon
	video_producer_demo.media_addon
	video_window_demo.media_addon
	vst_host.media_addon
	writer.media_addon
)
VosKeepTargets(MEDIA_ADDONS)
ImageInclude("/system/add-ons/media" ${MEDIA_ADDONS})

# Classic Media Kit codec plugins (src/add-ons/media/plugins), loaded by
# libmedia's PluginManager from add-ons/media/plugins.
set(MEDIA_PLUGINS
	ffmpeg
	amduvd
	au_reader
	dvd_streamer
	http_streamer
	raw_decoder
)
VosKeepTargets(MEDIA_PLUGINS)
ImageInclude("/system/add-ons/media/plugins" ${MEDIA_PLUGINS})

# Printer drivers and transports (src/add-ons/print) for print_server's
# printers that are not CUPS queues; the file names are what a printer
# folder records as its driver and transport.
set(PRINT_DRIVERS
	print_lips3
	print_lips4
	print_gutenprint
	print_pcl5
	print_pcl6
	print_ps
	print_preview
#	print_lpstyl: not in air/OS's image either (unfinished driver)
)
VosKeepTargets(PRINT_DRIVERS)
ImageInclude("/system/add-ons/Print" ${PRINT_DRIVERS})

set(PRINT_TRANSPORTS
	print_jetdirect
	print_ipp
	print_lpr
	print_parallel
	print_to_file
	print_serial
	print_usb
)
VosKeepTargets(PRINT_TRANSPORTS)
ImageInclude("/system/add-ons/Print/transport" ${PRINT_TRANSPORTS})

# mail_daemon add-ons (src/add-ons/mail_daemon), found by BMailSettings
# under add-ons/mail_daemon/<kind> by file name.
set(MAIL_INBOUND_PROTOCOLS IMAP POP3)
VosKeepTargets(MAIL_INBOUND_PROTOCOLS)
ImageInclude("/system/add-ons/mail_daemon/inbound_protocols" ${MAIL_INBOUND_PROTOCOLS})
set(MAIL_OUTBOUND_PROTOCOLS SMTP)
VosKeepTargets(MAIL_OUTBOUND_PROTOCOLS)
ImageInclude("/system/add-ons/mail_daemon/outbound_protocols" ${MAIL_OUTBOUND_PROTOCOLS})
set(MAIL_INBOUND_FILTERS MatchHeader NewMailNotification SpamFilter)
VosKeepTargets(MAIL_INBOUND_FILTERS)
ImageInclude("/system/add-ons/mail_daemon/inbound_filters" ${MAIL_INBOUND_FILTERS})
set(MAIL_OUTBOUND_FILTERS Fortune)
VosKeepTargets(MAIL_OUTBOUND_FILTERS)
ImageInclude("/system/add-ons/mail_daemon/outbound_filters" ${MAIL_OUTBOUND_FILTERS})

# Menu label is the file name; the kit loads from .../Screen Savers/<name>.
# leaves is not listed: air/OS dropped the Leaves saver, so this tree has
# no sources for it.
set(SCREEN_SAVERS
	butterfly
	debugnow
	flurry
	glife
	gravity
	icons
	ifs
	MessageSaver
	nebula
	shelf
	simpleclock
	slideshowsaver
	spider
)
VosKeepTargets(SCREEN_SAVERS)
ImageInclude("/system/add-ons/Screen Savers" ${SCREEN_SAVERS})

# index_server's analysers (src/add-ons/index_server), loaded from
# add-ons/index_server.
set(INDEX_SERVER_ADDONS
	AudioTagAnalyser
	FullTextAnalyser
)
VosKeepTargets(INDEX_SERVER_ADDONS)
ImageInclude("/system/add-ons/index_server" ${INDEX_SERVER_ADDONS})

# quick_look_server's previewers (src/add-ons/quick_look), loaded from
# add-ons/quick_look.
set(QUICK_LOOK_ADDONS
	ImagePreview
	PDFPreview
	TextPreview
)
VosKeepTargets(QUICK_LOOK_ADDONS)
ImageInclude("/system/add-ons/quick_look" ${QUICK_LOOK_ADDONS})

# Neural Kit back ends (src/add-ons/neural), found by NeuralRoster in
# add-ons/neural as <name>.neural. amdxdna is built on x86_64 only.
set(NEURAL_BACKENDS
	vulkan.neural
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
	list(APPEND NEURAL_BACKENDS amdxdna.neural)
endif()
VosKeepTargets(NEURAL_BACKENDS)
ImageInclude("/system/add-ons/neural" ${NEURAL_BACKENDS})

# The Network preference's add-ons (src/add-ons/network_settings), loaded
# from "add-ons/Network Settings" by file name.
set(NETWORK_SETTINGS_ADDONS
	Hostname
)
VosKeepTargets(NETWORK_SETTINGS_ADDONS)
ImageInclude("/system/add-ons/Network Settings" ${NETWORK_SETTINGS_ADDONS})


include(${CMAKE_CURRENT_LIST_DIR}/preferences.cmake)
# after apps.cmake and preferences.cmake: it reads their install lists
include(${CMAKE_CURRENT_LIST_DIR}/deskbar.cmake)


include(${CMAKE_CURRENT_LIST_DIR}/bin.cmake)


# App attrs tarball: each Application/Server/AddOn macro creates a zero-byte
# placeholder at apps_attrs_staging/${name} with BeOS xattrs written by
# resattr -O. Here we arrange them into install paths and tar with --xattrs.
# postinst extracts to a temp dir and applies attrs to the real installed binaries.
set(_FLAT   "${CMAKE_BINARY_DIR}/apps_attrs_staging")
set(_FINAL  "${CMAKE_BINARY_DIR}/apps_attrs_final")
set(_TAR    "${CMAKE_BINARY_DIR}/apps_attrs.tar")

set(_ARRANGE_CMDS
    COMMAND ${CMAKE_COMMAND} -E rm -rf   "${_FINAL}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/apps"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/servers"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/preferences"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/add-ons/Tracker"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FINAL}/system/add-ons/Translators"
)

foreach(_app ${SYSTEM_APPS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/apps/${_app}"
    )
endforeach()

foreach(_app ${DESKBAR_DEMOS_TARGETS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/apps/${_app}"
    )
endforeach()

foreach(_app ${DESKBAR_APPLETS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/apps/${_app}"
    )
endforeach()

foreach(_app ${CORE_APPLICATIONS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_app}" "${_FINAL}/system/${_app}"
    )
endforeach()

foreach(_server ${SYSTEM_SERVERS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_server}" "${_FINAL}/system/servers/${_server}"
    )
endforeach()

# SYSTEM_PREFERENCES_TARGETS and SYSTEM_PREFERENCES are parallel lists:
# targets have cmake-unique names; SYSTEM_PREFERENCES has the installed output names.
# (Deskbar_prefs/Tracker_prefs clash with the app targets of the same name in Jam,
# which uses per-subdirectory scoping — cmake uses a single global target namespace.)
list(LENGTH SYSTEM_PREFERENCES_TARGETS _nprefs)
math(EXPR _nprefs_last "${_nprefs} - 1")
# ported subset: the list can be empty, and RANGE -1 would still iterate.
if(_nprefs GREATER 0)
foreach(_i RANGE ${_nprefs_last})
    list(GET SYSTEM_PREFERENCES_TARGETS ${_i} _target)
    list(GET SYSTEM_PREFERENCES         ${_i} _outname)
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_target}" "${_FINAL}/system/preferences/${_outname}"
    )
endforeach()
endif()

foreach(_addon ${TRACKER_ADDONS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_addon}" "${_FINAL}/system/add-ons/Tracker/${_addon}"
    )
endforeach()

foreach(_translator ${SYSTEM_TRANSLATORS})
    list(APPEND _ARRANGE_CMDS
        COMMAND cp -a "${_FLAT}/${_translator}" "${_FINAL}/system/add-ons/Translators/${_translator}"
    )
endforeach()

list(APPEND _ARRANGE_CMDS
    COMMAND tar --xattrs -cf "${_TAR}" -C "${_FINAL}" .
)

# ported subset: the web kit is not in this tree yet.
set(_VOS_WEB_TARGET "")
if(TARGET web)
    set(_VOS_WEB_TARGET web)
endif()

add_custom_target(apps_attrs ALL
    ${_ARRANGE_CMDS}
    DEPENDS ${SYSTEM_APPS} ${DESKBAR_DEMOS_TARGETS} ${DESKBAR_APPLETS} ${CORE_APPLICATIONS} ${SYSTEM_SERVERS} ${SYSTEM_PREFERENCES_TARGETS} ${TRACKER_ADDONS} ${SYSTEM_TRANSLATORS} ${_VOS_WEB_TARGET}
    COMMENT "Packaging app attrs"
)

install(FILES "${_TAR}" DESTINATION /usr/share/vos COMPONENT data)


# Tracker "New" templates need BEOS:TYPE xattrs so Tracker's New submenu
# can classify them and pick the right MIME icon. Source files in the tree
# carry no xattrs (git doesn't preserve them); stage + tar with --xattrs.
set(_TPL_SRC    "${CMAKE_SOURCE_DIR}/src/data/settings/tracker_new_templates")
set(_TPL_STAGE  "${CMAKE_BINARY_DIR}/templates_staging")
set(_TPL_TAR    "${CMAKE_BINARY_DIR}/templates_attrs.tar")

# Template -> BEOS:TYPE mapping. SMIM prefix is the B_MIME_STRING_TYPE
# type code (see Haiku's TypeConstants.h).
set(_TPL_NAMES        "C++ header" "C++ source" "Makefile" "Person"          "text file")
set(_TPL_MIME_TYPES   "text/x-source-code"
                      "text/x-source-code"
                      "text/x-makefile"
                      "application/x-person"
                      "text/plain")

set(_TPL_CMDS
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_TPL_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_TPL_STAGE}"
)
list(LENGTH _TPL_NAMES _ntpl)
math(EXPR _ntpl_last "${_ntpl} - 1")
foreach(_i RANGE ${_ntpl_last})
    list(GET _TPL_NAMES      ${_i} _name)
    list(GET _TPL_MIME_TYPES ${_i} _mime)
    list(APPEND _TPL_CMDS
        COMMAND ${CMAKE_COMMAND} -E copy "${_TPL_SRC}/${_name}" "${_TPL_STAGE}/${_name}"
        COMMAND setfattr -n user.beos.BEOS:TYPE -v "SMIM${_mime}" "${_TPL_STAGE}/${_name}"
    )
endforeach()
list(APPEND _TPL_CMDS
    COMMAND tar --xattrs -cf "${_TPL_TAR}" -C "${_TPL_STAGE}" .
)

add_custom_target(templates_attrs ALL
    ${_TPL_CMDS}
    COMMENT "Packaging Tracker template attrs"
)

install(FILES "${_TPL_TAR}" DESTINATION /usr/share/vos COMPONENT data)

# Boot splash (vos-splash) and its boot integration.
include(${CMAKE_CURRENT_LIST_DIR}/splash.cmake)
