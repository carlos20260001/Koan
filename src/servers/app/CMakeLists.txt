find_package(Freetype REQUIRED)

find_library(DRM_LIBRARY NAMES drm)
find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

# TODO would be cool if DeclareDependency in deps.cmake accepted
# a parameter that will use PkgConfig automatically when available
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSEAT REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(GBM gbm)

if(GBM_FOUND)
	add_definitions(-DHAVE_GBM)
endif()

# 2.4.97 is the committed floor: the last libdrm symbol the DRM backend
# plans to need lands there. Below it, or on a builder with old kernel
# headers, the HAVE_DRM_* guards turn the affected paths off and
# app_server still builds, legacy-only.
pkg_check_modules(LIBDRM libdrm>=2.4.97)
if(NOT LIBDRM_FOUND)
	message(STATUS "libdrm older than 2.4.97: DRM backend builds "
		"legacy-only via the HAVE_DRM_* guards")
endif()

include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${DRM_INCLUDE_DIR} ${DRM_INCLUDE_DIR}/libdrm)
# The probes link, so the library has to be on their link line too.
set(CMAKE_REQUIRED_LIBRARIES ${DRM_LIBRARY})
check_symbol_exists(drmModeAtomicCommit "xf86drmMode.h" HAVE_DRM_ATOMIC)
check_symbol_exists(drmModeCreateLease "xf86drmMode.h" HAVE_DRM_LEASE)
check_symbol_exists(drmModeFormatModifierBlobIterNext "xf86drmMode.h"
	HAVE_DRM_FMTMOD_ITER)
check_symbol_exists(drmModeAddFB2WithModifiers "xf86drmMode.h"
	HAVE_DRM_ADDFB2_MODS)
unset(CMAKE_REQUIRED_LIBRARIES)

if(HAVE_DRM_ATOMIC)
	add_definitions(-DHAVE_DRM_ATOMIC)
endif()
if(HAVE_DRM_LEASE)
	add_definitions(-DHAVE_DRM_LEASE)
endif()
if(HAVE_DRM_FMTMOD_ITER)
	add_definitions(-DHAVE_DRM_FMTMOD_ITER)
endif()
if(HAVE_DRM_ADDFB2_MODS)
	add_definitions(-DHAVE_DRM_ADDFB2_MODS)
endif()


# Display backends. DRM and fbdev are always built; the multi-output
# KMS/GBM/Mesa backend in drawing/interface/linux/kms/ is added when its
# CMakeLists.txt exists. That file is expected to set APP_SERVER_KMS_SRCS
# (sources relative to this directory) and optionally APP_SERVER_KMS_LIBS
# and APP_SERVER_KMS_INCLUDES, and to provide
#	HWInterface* create_kms_hw_interface();
# which returns NULL (or an interface whose InitCheck() fails) when no
# KMS device can be used. ScreenManager picks the backend at run time
# through VOS_DISPLAY_BACKEND=kms|drm|fbdev (default: kms, drm, fbdev).
set(APP_SERVER_KMS_SRCS "")
set(APP_SERVER_KMS_LIBS "")
set(APP_SERVER_KMS_INCLUDES "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/drawing/interface/linux/kms/CMakeLists.txt")
	include("${CMAKE_CURRENT_SOURCE_DIR}/drawing/interface/linux/kms/CMakeLists.txt")
endif()

Server(
	app_server

	SOURCES
		Angle.cpp
		AppServer.cpp
		BitmapManager.cpp
		Canvas.cpp
		ClientMemoryAllocator.cpp
		CursorData.cpp
		CursorManager.cpp
		CursorSet.cpp
		DelayedMessage.cpp
		Desktop.cpp
		DesktopListener.cpp
		DesktopSettings.cpp
		DirectWindowInfo.cpp
		DisplayChangeTracker.cpp
		DisplayLayout.cpp
		DrawState.cpp
		EventDispatcher.cpp
		PowerManager.cpp
		EventStream.cpp
		InputManager.cpp
		IntPoint.cpp
		IntRect.cpp
		Layer.cpp
		MessageLooper.cpp
		MultiLocker.cpp
		OffscreenServerWindow.cpp
		OffscreenWindow.cpp
		OutputAddonManager.cpp
		PictureBoundingBoxPlayer.cpp
		ProfileMessageSupport.cpp
		RGBColor.cpp
		RegionPool.cpp
		Screen.cpp
		ScreenConfigurations.cpp
		ScreenManager.cpp
		ScreenRelayout.cpp
		ServerApp.cpp
		ServerBitmap.cpp
		ServerCursor.cpp
		ServerFont.cpp
		ServerPicture.cpp
		ServerWindow.cpp
		SystemPalette.cpp
		View.cpp
		VirtualOutput.cpp
		VirtualScreen.cpp
		Window.cpp
		WindowList.cpp
		../../kits/shared/PointerControlCore.cpp
		Workspace.cpp
		WorkspacesView.cpp

		decorator/Decorator.cpp
		decorator/DecorManager.cpp
		decorator/DefaultDecorator.cpp
		decorator/DefaultWindowBehaviour.cpp
		decorator/MagneticBorder.cpp
		decorator/TabDecorator.cpp
		decorator/WindowBehaviour.cpp

		font/AppFontManager.cpp
		font/ColorGlyphRenderer.cpp
		font/FontCache.cpp
		font/FontCacheEntry.cpp
		font/FontEngine.cpp
		font/FontFamily.cpp
		font/FontManager.cpp
		font/FontStyle.cpp
		font/GlobalFontManager.cpp
		font/OpenTypeLigatures.cpp

		# TODO this should be
		#libasdrawing.a
		drawing/AlphaMask.cpp
		drawing/AlphaMaskCache.cpp
		drawing/BitmapBuffer.cpp
		drawing/BitmapDrawingEngine.cpp
		drawing/drawing_support.cpp
		drawing/DrawingEngine.cpp
		drawing/MallocBuffer.cpp
		drawing/PatternHandler.cpp
		drawing/Overlay.cpp
		drawing/ScaledReadback.cpp
		drawing/BitmapHWInterface.cpp
		drawing/BBitmapBuffer.cpp
		drawing/HWInterface.cpp

		drawing/Painter/GlobalSubpixelSettings.cpp
		drawing/Painter/Painter.cpp
		drawing/Painter/Transformable.cpp
		# drawing_modes
		drawing/Painter/drawing_modes/PixelFormat.cpp
		# bitmap_painter
		drawing/Painter/bitmap_painter/BitmapPainter.cpp
		drawing/Painter/AGGTextRenderer.cpp

		#drawing/interface/remote/NetReceiver.cpp
		#drawing/interface/remote/NetSender.cpp
		#drawing/interface/remote/RemoteDrawingEngine.cpp
		#drawing/interface/remote/RemoteEventStream.cpp
		#drawing/interface/remote/RemoteHWInterface.cpp
		#drawing/interface/remote/RemoteMessage.cpp
		#drawing/interface/remote/StreamingRingBuffer.cpp

		#drawing/interface/local/AccelerantBuffer.cpp
		#drawing/interface/local/AccelerantHWInterface.cpp

		drawing/interface/linux/drm/DrmHWInterface.cpp
		drawing/interface/linux/drm/DrmBuffer.cpp
		drawing/interface/linux/drm/BufferAgeTracker.cpp
		drawing/interface/linux/drm/modeset.c

		drawing/interface/linux/fbdev/FBDevHWInterface.cpp
		drawing/interface/linux/fbdev/FBDevBuffer.cpp

		# the multi-output KMS/GBM backend (M9-W3 design), when present
		${APP_SERVER_KMS_SRCS}
		#drawing/interface/linux/fbdev/TTy.cpp

		#drawing/interface/linux/LibInputEventStream.cpp
		# LibEvdevEventStream: fallback event stream (hardcoded US
		# scancode table, no xkbcommon). Dormant unless the caller
		# sets APP_SERVER_EMBED_INPUT=1 at spawn time — the default
		# input path is input_server, run pre-auth via janus.
		drawing/interface/linux/LibEvdevEventStream.cpp

		stackandtile/SATDecorator.cpp
		stackandtile/SATGroup.cpp
		stackandtile/SATWindow.cpp
		stackandtile/StackAndTile.cpp
		stackandtile/Stacking.cpp
		stackandtile/Tiling.cpp

		# EDID decoder shared with Haiku's accelerants (DisplayLayout)
		${PROJECT_SOURCE_DIR}/src/add-ons/accelerants/common/decode_edid.c

	LIBS localestub shared agg icon linprog systemd ${FREETYPE_LIBRARY}
		${UDEV_LIBRARIES} ${DRM_LIBRARIES} ${SEAT_LIBRARIES}
		${LIBEVDEV_LIBRARIES} ${GBM_LIBRARIES} backlight
		${APP_SERVER_KMS_LIBS}

	RDEF app_server.rdef

	INCLUDES
	${DRM_INCLUDE_DIR}/libdrm/
	${FREETYPE_INCLUDE_DIRS}
	${LIBEVDEV_INCLUDE_DIRS}
	${GBM_INCLUDE_DIRS}
	"${PROJECT_SOURCE_DIR}/src/libs/backlight/"
	"./drawing"
	"./drawing/interface/linux"
	"./drawing/interface/linux/drm"
	"./drawing/interface/linux/fbdev"
	"./drawing/interface/local"
	"./drawing/interface/remote"
	"./font"
	"./stackandtile"
	"./decorator"
	"./drawing/Painter"
	"./drawing/Painter/drawing_modes"
	${APP_SERVER_KMS_INCLUDES}
)

if(APP_SERVER_KMS_SRCS)
	target_compile_definitions(app_server PRIVATE VOS_HAVE_KMS_BACKEND)
endif()

UsePrivateHeaders(app_server app interface graphics graphics/common input kernel support system)

# Decorator add-ons subclass app_server classes and resolve them here.
set_target_properties(app_server PROPERTIES ENABLE_EXPORTS ON)

#TODO: we want a custom target_include_directories that does that:
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -iquote ${PROJECT_SOURCE_DIR}/src/servers/app/")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -iquote ${PROJECT_SOURCE_DIR}/src/servers/app/ \
	-iquote ${PROJECT_SOURCE_DIR}/src/servers/app/drawing/Painter/bitmap_painter/ \
	-iquote ${PROJECT_SOURCE_DIR}/src/add-ons/input_server/devices/keyboard/")
