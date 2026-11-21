# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Tracker add-ons. Tracker lists every executable file it finds in the
# "Tracker" subdirectory of each add-ons directory (BPathFinder,
# B_FIND_PATH_ADD_ONS_DIRECTORY: /system/add-ons/Tracker in the image) and
# uses the file name as the menu label, so each add-on keeps the file name
# its Jamfile gave it through OUTPUT_NAME.
#
# Two kinds: plain add-ons (a shared object that Tracker loads with
# load_add_on() and calls process_refs() in), and add-ons that are also
# applications (main() plus process_refs() from TrackerAddOnAppLaunch.h,
# which launches the file itself). The second kind is a RunnableAddOn: a
# shared object with a PT_INTERP, so both dlopen() and execve() work.

if(NOT TARGET tracker)
	return()
endif()

add_subdirectory(filetype)
add_subdirectory(iconvader)
add_subdirectory(opentargetfolder)
add_subdirectory(openterminal)
add_subdirectory(zipomatic)

# Mail add-ons: they act on air/OS's mail files and link its mail kit.
if(TARGET mail)
	add_subdirectory(label_as)
	add_subdirectory(mark_as)
endif()
