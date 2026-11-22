# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Command line tools, from the groups of the old src/bin Jamfile. Each
# group lists commands built from one source file with the same
# libraries; libbe and libroot come with Application().

if(NOT BUILDTOOLS_MODE STREQUAL "1")

foreach(_dir
		addattr bfs_tools desklink jam keymap keystore listdev listusb locale
		mail_utils media_client network neural package package_repo pc query
		rc screen_blanker screenmode xclientsig)
	add_subdirectory(${_dir})
endforeach()

UsePrivateHeaders(GLOBAL app interface libroot kernel shared storage support
	system tracker)

# BinCommands(<libs...> FILES <sources...>): one command per source, named
# after it.
function(BinCommands)
	cmake_parse_arguments(_BC "" "" "LIBS;FILES" ${ARGN})
	foreach(_src IN LISTS _BC_FILES)
		get_filename_component(_name ${_src} NAME_WE)
		# the Jamfile gave every command haiku-utils.rsrc, plus its own
		set(_rdef haiku-utils.rdef)
		if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_name}.rdef)
			list(APPEND _rdef ${_name}.rdef)
		endif()
		Application(${_name} SOURCES ${_src} LIBS ${_BC_LIBS} RDEF ${_rdef})
	endforeach()
endfunction()

# no library beyond libroot/libbe
BinCommands(FILES
	catarea.c chop.c driveinfo.c error.c finddir.c get_driver_settings.cpp
	hd.c listarea.c listimage.c listport.c listsem.c lsindex.cpp prio.c
	release.c rmattr.cpp rmindex.cpp system_time.cpp unchop.c)

# Debian's fortune-mod owns "fortune"; the image's copy is fortune_vos.
Application(fortune_vos SOURCES fortune.c RDEF haiku-utils.rdef fortune.rdef)

# libbe
BinCommands(FILES
	beep.cpp catattr.cpp clipboard.cpp diskimage.cpp dpms.cpp draggers.cpp
	ffm.cpp iroster.cpp isvolume.cpp listattr.cpp listfont.cpp listres.cpp
	message.cpp mkindex.cpp modifiers.cpp mvattr.cpp quit.cpp roster.cpp
	setversion.cpp trash.cpp version.cpp WindowShade.cpp
	alert.cpp hey.cpp reindex.cpp resattr.cpp screeninfo.cpp
	setcontrollook.cpp setdecor.cpp settype.cpp spybmessage.cpp
	urlwrapper.cpp copyattr.cpp diff_zip.cpp mimeset.cpp mountvolume.cpp
	setmime.cpp seticon.cpp setapptype.cpp sysinfo.cpp xres.cpp
	open.cpp waitfor.cpp)

# libshared
BinCommands(LIBS shared FILES launch_roster.cpp)

# catalog stub
BinCommands(LIBS localestub FILES dstcheck.cpp)

# translation kit
BinCommands(LIBS translation FILES translate.cpp)
BinCommands(LIBS translation icon FILES notify.cpp)

# Tracker kit
BinCommands(LIBS tracker FILES checkitout.cpp)
BinCommands(LIBS tracker localestub FILES filepanel.cpp)

# classic Media Kit
BinCommands(LIBS media FILES installsound.cpp setvolume.cpp mediakey.cpp
	bt_a2dp.cpp)
foreach(_tool installsound setvolume mediakey bt_a2dp)
	UseMediaKit(${_tool})
endforeach()
target_include_directories(setvolume PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/osd)
target_include_directories(mediakey PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/media_controls)
target_link_libraries(mediakey PRIVATE media_controls)
target_include_directories(bt_a2dp PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/bluetooth)

# package kit (hpkg)
BinCommands(LIBS bpackage FILES findpaths.cpp)

# device and bluetooth kits (both in libbe here)
BinCommands(FILES setusbconfig.cpp bt_dev_info.cpp bt_discovery.cpp
	bt_le.cpp)
target_include_directories(bt_le PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/bluetooth)

else()

add_subdirectory(rc)

BuildModeApplication(resattr SOURCES resattr.cpp)
BuildModeApplication(xres SOURCES xres.cpp)

endif()
