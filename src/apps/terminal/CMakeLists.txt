# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's Terminal. The shell runs on a Linux pseudo terminal
# (posix_openpt/grantpt/unlockpt, TIOCSWINSZ), the login shell comes from
# the passwd database; the foreground process and its working directory
# come from libroot2's /proc-based team information.
foreach(_lib tracker shared translation textencoding localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Terminal: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

find_package(Freetype REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL)

Application(
	Terminal

	SOURCES
	ActiveProcessInfo.cpp
	AppearPrefView.cpp
	Arguments.cpp
	Colors.cpp
	FindWindow.cpp
	Globals.cpp
	HyperLink.cpp
	ImageCodecs.cpp
	InlineInput.cpp
	PatternEvaluator.cpp
	PrefHandler.cpp
	PrefWindow.cpp
	SetTitleDialog.cpp
	Shell.cpp
	ShellInfo.cpp
	ShellParameters.cpp
	SmartTabView.cpp
	TermApp.cpp
	TermCrypto.cpp
	TermDragAndDrop.cpp
	TermFileTransfer.cpp
	TerminalCharClassifier.cpp
	TerminalRoster.cpp
	TermConst.cpp
	TermScrollView.cpp
	TermView.cpp
	TermViewStates.cpp
	TermWindow.cpp
	ThemeView.cpp
	ThemeWindow.cpp
	TitlePlaceholderMapper.cpp

	emulator/Base64.cpp
	emulator/ColorNames.cpp
	emulator/Terminal.cpp
	emulator/TerminalCommands.cpp
	emulator/TerminalGraphics.cpp
	emulator/TerminalHistory.cpp
	emulator/TerminalLinks.cpp
	emulator/TerminalInput.cpp
	emulator/TerminalSequences.cpp
	emulator/UnicodeTables.cpp
	emulator/VTParser.cpp

	render/BoxDrawing.cpp
	render/ColorEmoji.cpp
	render/FrameBuilder.cpp
	render/GlyphCache.cpp
	render/Ligatures.cpp
	render/SoftwareRenderer.cpp

	LIBS tracker shared translation textencoding localestub
		Freetype::Freetype ZLIB::ZLIB
	RDEF Terminal.rdef XColors.rdef
)

UsePrivateHeaders(Terminal libroot notification shared system textencoding)
target_include_directories(Terminal PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/emulator
	${CMAKE_CURRENT_SOURCE_DIR}/render
	${PROJECT_SOURCE_DIR}/src/kits/tracker
)
if(OPENSSL_FOUND)
	target_compile_definitions(Terminal PRIVATE
		OPENSSL_ENABLED OPENSSL_API_COMPAT=10101 OPENSSL_NO_DEPRECATED=1)
	target_link_libraries(Terminal PUBLIC OpenSSL::Crypto)
endif()

DoCatalogs("x-vnd.vos-Terminal" apps/terminal)

add_subdirectory(render/opengl)
