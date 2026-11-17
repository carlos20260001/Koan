# ported subset: upstream's src/tests/kits/interface/CMakeLists.txt also
# builds the cppunit add-on "interfacetest" (no cppunit in this tree yet)
# and Test() programs whose sources air/OS and VitruvianOS both changed
# (ChannelSlider, ClippingPlusRedraw, GetMouse, Screen, Slider; ArrowShape
# is VitruvianOS-only): W4 merges them with the rest of src/tests.

UsePrivateHeaders(GLOBAL interface)

add_definitions(-DTEST_OBOS)

# Host tests: pure interface kit code, no app_server and no nexus.ko.
# The sources stay at VitruvianOS's paths under src/tests/vos.

# Output add-on transport core: rect math, generation handoff, frame
# encode/decode and the shared area layout.
add_executable(test_output_addon_core
	${PROJECT_SOURCE_DIR}/src/tests/vos/output_addon/output_addon_host_test.cpp
	${PROJECT_SOURCE_DIR}/src/kits/interface/OutputAddonCore.cpp
)
target_include_directories(test_output_addon_core PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/interface)
set_target_properties(test_output_addon_core PROPERTIES COMPILE_FLAGS
	"-include LinuxBuildCompatibility.h")
add_test(NAME interface_output_addon_core COMMAND test_output_addon_core)

# Panel orientation rules (PanelOrientationTransform.h, header only).
add_executable(test_panel_orientation
	${PROJECT_SOURCE_DIR}/src/tests/vos/test_panel_orientation.cpp
)
target_include_directories(test_panel_orientation PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/interface)
set_target_properties(test_panel_orientation PROPERTIES COMPILE_FLAGS
	"-include LinuxBuildCompatibility.h")
add_test(NAME interface_panel_orientation COMMAND test_panel_orientation)

# Interactive programs: they need app_server (and nexus.ko), so they are
# only built.
Test(ControlLookTest SOURCES ControlLookTest.cpp)
Test(DrawBitmapTest SOURCES bbitmap/DrawBitmapTest.cpp)
if(TARGET tracker)
	Test(LookTest SOURCES look/Look.cpp LIBS tracker)
endif()
Test(ListViewTest SOURCES ListViewTest.cpp)
Test(MenuBeginningTest SOURCES MenuBeginningTest.cpp)
Test(PulseTest SOURCES PulseTest.cpp)
Test(ScrollViewTest SOURCES ScrollViewTest.cpp)
Test(SetDiskModeTest SOURCES SetDiskModeTest.cpp)
Test(SimpleAlertTest SOURCES SimpleAlertTest.cpp)
Test(StatusBarTest SOURCES StatusBarTest.cpp)
Test(TextViewTestManual SOURCES TextViewTestManual.cpp)
Test(ToolTipTest SOURCES ToolTipTest.cpp)
Test(TruncateString SOURCES TruncateString.cpp)
Test(WindowStackTest SOURCES bwindowstack/WindowStackTest.cpp)
