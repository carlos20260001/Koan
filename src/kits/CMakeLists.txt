# ported subset: only be_bootstrap, the static support-kit archive that root
# (libroot.so) links, is on CMake here. The libbe target ("be"), its
# dependencies (ICU, gio, libusb, ...) and the other kits arrive with the
# kits milestone; src/CMakeLists.txt adds the layers above the kits only
# once a "be" target exists.

AddOn(
	be_bootstrap
	STATIC

	LIBS pthread ${ZLIB_LIBRARIES}
)

UsePrivateHeaders(be_bootstrap interface app support)

add_subdirectory(support)
