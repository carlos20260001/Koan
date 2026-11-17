# ported subset: the plain build container has no libnm-dev (the bake
# chroot installs it from build/deps.cmake). Without it the kit is left out
# of be, with a warning, so configure stays usable; upstream requires it.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(LIBNM libnm)
endif()
if(NOT LIBNM_FOUND)
	message(WARNING "network kit: libnm not found (libnm-dev); the network "
		"kit is left out of be")
	return()
endif()

target_sources(be PRIVATE
	libnetapi/AbstractSocket.cpp
	libnetapi/Certificate.cpp
	libnetapi/DatagramSocket.cpp
	libnetapi/DynamicBuffer.cpp
	libnetapi/NetAddress.cpp
	libnetapi/NetBuffer.cpp
	libnetapi/NetDebug.cpp
	libnetapi/NetEndpoint.cpp
	libnetapi/NetworkAddress.cpp
	libnetapi/NetworkAddressResolver.cpp
	libnetapi/NetworkDevice.cpp
	libnetapi/NetworkInterface.cpp
	libnetapi/NetworkRoster.cpp
	libnetapi/NetworkRoute.cpp
	libnetapi/NMBackend.cpp
	libnetapi/ModemManagerBackend.cpp
	libnetapi/NetworkSettings.cpp
	libnetapi/notifications.cpp
	libnetapi/ProxySecureSocket.cpp
	libnetapi/SecureSocket.cpp
	libnetapi/Socket.cpp
	libnetapi/SocketMessenger.cpp

	libnetservices/DataRequest.cpp
	libnetservices/FileRequest.cpp
	libnetservices/Geolocation.cpp
	libnetservices/GopherRequest.cpp
	libnetservices/HttpAuthentication.cpp
	libnetservices/HttpForm.cpp
	libnetservices/HttpHeaders.cpp
	libnetservices/HttpRequest.cpp
	libnetservices/HttpResult.cpp
	libnetservices/HttpTime.cpp
	libnetservices/NetworkCookie.cpp
	libnetservices/NetworkCookieJar.cpp
	libnetservices/NetworkRequest.cpp
	libnetservices/UrlContext.cpp
	libnetservices/UrlProtocolAsynchronousListener.cpp
	libnetservices/UrlProtocolDispatchingListener.cpp
	libnetservices/UrlProtocolListener.cpp
	libnetservices/UrlProtocolRoster.cpp
	libnetservices/UrlRequest.cpp
	libnetservices/UrlResult.cpp
	libnetservices/UrlSynchronousRequest.cpp

	# libnetservices2 (BHttpSession et al.) is the netservices2 static
	# library below, not part of be.
)

target_compile_definitions(be PRIVATE OPENSSL_ENABLED)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libnetapi
	${CMAKE_SOURCE_DIR}/headers/private/net
	${CMAKE_SOURCE_DIR}/headers/private/netservices
)

# libnetservices and libnetservices2 both ship files with the same name
# (HttpTime.h, HttpRequest.h, HttpResult.h -- old vs. new API). The shared
# "be" target has one flat include path, so netservices2's own sources use
# quoted relative includes for those specific headers to bypass the
# ambiguity instead of relying on -I search order.

# Find NetworkManager libraries
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(LIBNM REQUIRED libnm)
	if(LIBNM_FOUND)
		target_include_directories(be PRIVATE ${LIBNM_INCLUDE_DIRS})
		target_link_libraries(be PRIVATE ${LIBNM_LIBRARIES})
		target_compile_options(be PRIVATE ${LIBNM_CFLAGS_OTHER})
	endif()
endif()

# air/OS: libnetservices2 (BHttpSession) stays a static library of its own,
# as in Haiku, so its HttpRequest.h/HttpResult.h/HttpTime.h never meet
# libnetservices' headers of the same names on be's include path. Its
# consumers are the netservices2 tests.
AddOn(
	netservices2
	STATIC

	SOURCES
	libnetservices2/ErrorsExt.cpp
	libnetservices2/HttpBuffer.cpp
	libnetservices2/HttpFields.cpp
	libnetservices2/HttpParser.cpp
	libnetservices2/HttpRequest.cpp
	libnetservices2/HttpResult.cpp
	libnetservices2/HttpSerializer.cpp
	libnetservices2/HttpSession.cpp
	libnetservices2/HttpTime.cpp
	libnetservices2/NetServicesMisc.cpp
)

target_include_directories(netservices2 PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/netservices2
)
UsePrivateHeaders(netservices2 net shared support)
target_compile_options(netservices2 PRIVATE -std=c++17)