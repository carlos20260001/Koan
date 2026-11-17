# air/OS's debug kit (libdebug.so): team debugging, debug contexts and
# symbol lookup for the debugger kit, debug_server, strace and profile.
#
# ported subset: not built yet. It is a client of Haiku's kernel debugger
# API (install_team_debugger(), debug nub ports, the runtime loader's image
# list), which has no Linux counterpart.
# TODO(linux backend): DebugContext/TeamDebugger on ptrace(2) and
# waitpid(), SymbolLookup on /proc/<pid>/maps and the ELF files, then turn
# VOS_WITH_DEBUG_KIT on. The source list mirrors the retired Jamfile.
option(VOS_WITH_DEBUG_KIT "Build the (not yet ported) debug kit" OFF)
if(NOT VOS_WITH_DEBUG_KIT)
	return()
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
	set(VOS_DEBUG_ARCH x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
	set(VOS_DEBUG_ARCH arm64)
else()
	set(VOS_DEBUG_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif()

AddOn(
	debug
	SHARED

	SOURCES
	debug_support.cpp
	DebugContext.cpp
	DebugEventStream.cpp
	DebugLooper.cpp
	DebugMessageHandler.cpp
	Image.cpp
	SymbolLookup.cpp
	TeamDebugger.cpp

	# architecture specific
	arch/${VOS_DEBUG_ARCH}/arch_debug_support.cpp

	LIBS be root
)

target_include_directories(debug PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/arch
	${CMAKE_SOURCE_DIR}/headers/private/debug
	${CMAKE_SOURCE_DIR}/headers/private/runtime_loader
)
UsePrivateHeaders(debug libroot shared system)
