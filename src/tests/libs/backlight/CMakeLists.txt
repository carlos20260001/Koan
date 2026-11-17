# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

find_path(DRM_INCLUDE_DIR NAMES libdrm/drm.h)

Test(
	test_backlight_selection
	SOURCES
		test_backlight_selection.c
		${PROJECT_SOURCE_DIR}/src/libs/backlight/libbacklight.c
	INCLUDES
		${PROJECT_SOURCE_DIR}/src/libs/backlight/
		${DRM_INCLUDE_DIR}/libdrm/
	LIBS
		udev
)

Test(
	test_drm_card_rank
	SOURCES
		test_drm_card_rank.cpp
	INCLUDES
		${PROJECT_SOURCE_DIR}/headers/private/
		${DRM_INCLUDE_DIR}/libdrm/
	LIBS
		seat
)

# ported subset: CTest runs host builds of the two fixtures (the Test()
# programs above link libroot, which needs nexus.ko), as
# run_host_test.sh builds them.
add_executable(test_backlight_selection_host
	test_backlight_selection.c
	${PROJECT_SOURCE_DIR}/src/libs/backlight/libbacklight.c
)
target_include_directories(test_backlight_selection_host PRIVATE
	${PROJECT_SOURCE_DIR}/src/libs/backlight/
	${DRM_INCLUDE_DIR}/libdrm/
)
target_link_libraries(test_backlight_selection_host PRIVATE udev)
add_test(NAME backlight_selection COMMAND test_backlight_selection_host)

add_executable(test_drm_card_rank_host test_drm_card_rank.cpp)
target_include_directories(test_drm_card_rank_host PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/
	${DRM_INCLUDE_DIR}/libdrm/
)
target_link_libraries(test_drm_card_rank_host PRIVATE seat)
add_test(NAME backlight_drm_card_rank COMMAND test_drm_card_rank_host)
