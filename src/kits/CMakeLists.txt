find_package(ICU REQUIRED COMPONENTS i18n uc dt in io)

# ported subset: the support, app and storage kits are on CMake (W1-K1),
# interface and locale since W2; W2 also adds translation, shared,
# tracker, ... Each kit below is added only once its CMakeLists.txt
# exists, and "be" links the companion libraries (icon, agg, localestub,
# shared) only once they have a target, see _vos_be_link_companions at the
# end. OpenSSL is linked only with the kits that use it (network, shared).
AddOn(
	be
	SHARED

	LIBS root ${ICU_LIBRARIES} ${ZLIB_LIBRARIES} pthread systemd

	RDEF
	CountryFlags.rdef
	Icons.rdef
	LanguageFlags.rdef
	libbe_version.rdef
)

# air/OS: Icons.rdef holds the system icons BIconUtils::GetSystemIcon()
# reads from libbe.so (alert icons); libbe_version.rdef its version and
# signature. VitruvianOS's libbe carries only the flags.

UsePrivateHeaders(be app interface kernel print shared locale libroot shared system)

target_compile_options(be PRIVATE -std=c++17)
# Debug-only vref tracing, off by default: set VREF_TRACE=/dir and re-add this.
# target_compile_definitions(be PRIVATE ENABLE_VREF_TRACKING)

# Both the network kit (libnm) and the bluetooth kit (BlueZBackend.cpp, via
# gio) talk D-Bus through glib. Declared once here rather than in each kit's
# own CMakeLists.txt so it isn't pulled in twice; PRIVATE to "be" is load-
# bearing -- it keeps GLib/GDBus headers out of src/apps and src/preferences,
# which is what stops those layers from calling D-Bus directly and bypassing
# NMBackend/BlueZBackend.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	# ported subset: only once the network or bluetooth kit is here
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/network/CMakeLists.txt"
			OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bluetooth/CMakeLists.txt")
		pkg_check_modules(GIO REQUIRED gio-2.0)
		if(GIO_FOUND)
			target_include_directories(be PRIVATE ${GIO_INCLUDE_DIRS})
			target_link_libraries(be PRIVATE ${GIO_LIBRARIES})
			target_compile_options(be PRIVATE ${GIO_CFLAGS_OTHER})
		endif()
	endif()

	# The USB kit (src/kits/device) enumerates through libudev and moves
	# data through libusb-1.0; libusb.h sits in its own include dir on
	# Debian, hence pkg-config rather than a bare -l.
	# ported subset: only once the device kit is here
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/device/CMakeLists.txt")
		pkg_check_modules(USBKIT REQUIRED libusb-1.0 libudev)
		if(USBKIT_FOUND)
			target_include_directories(be PRIVATE ${USBKIT_INCLUDE_DIRS})
			target_link_libraries(be PRIVATE ${USBKIT_LIBRARIES})
			target_compile_options(be PRIVATE ${USBKIT_CFLAGS_OTHER})
		endif()
	endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/network/CMakeLists.txt"
		OR EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shared/CMakeLists.txt")
	target_link_libraries(be PRIVATE ${OPENSSL_LIBRARIES})
endif()

AddOn(
	be_bootstrap
	STATIC

	LIBS pthread ${ZLIB_LIBRARIES}
)

UsePrivateHeaders(be_bootstrap interface app support)

# ported subset: upstream adds every kit unconditionally.
foreach(_kit
		accounts app bluetooth device game hwaccel interface locale
		location media2 midi midi2 network opengl package screensaver
		shared storage support telephony textencoding translation tracker
		web)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_kit}/CMakeLists.txt")
		add_subdirectory(${_kit})
	endif()
endforeach()
DoCatalogs("x-vnd.vos-libbe" kits)

# ported subset: upstream lists icon, agg, localestub and shared in LIBS
# above. They are defined in src/libs and src/kits/shared, which W2 puts on
# CMake, so link each one at the end of the configure step if it exists.
function(_vos_be_link_companions)
	foreach(_lib icon agg localestub shared)
		if(TARGET ${_lib})
			target_link_libraries(be PRIVATE ${_lib})
		endif()
	endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
	CALL _vos_be_link_companions)
