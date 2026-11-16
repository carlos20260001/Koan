# Linux's drm/imagination (upstream/, see upstream/README), compiled as C
# against compat/linux_compat.h, with native replacements in glue/. The
# display driver's R_TWI controller (../sunxi_display/twi.cpp) drives the
# PMIC.
set(_sunxi_display ${CMAKE_CURRENT_SOURCE_DIR}/../sunxi_display)
set(_compat ${CMAKE_CURRENT_SOURCE_DIR}/compat)
set(_glue ${CMAKE_CURRENT_SOURCE_DIR}/glue)
set(_upstream ${CMAKE_CURRENT_SOURCE_DIR}/upstream)

HaikuGpuKernelAddon(powervr
	SOURCES
		A733Power.cpp
		driver.cpp
		PvrDevice.cpp
		twi.cpp

		# compat/
		linux_compat.c
		linux_compat_haiku.cpp
		linux_compat_sched.c

		# glue/
		pvr_haiku_device.c
		pvr_haiku_drm.c
		pvr_haiku_drv.c		# includes upstream/pvr_drv.c
		pvr_haiku_gem.c
		pvr_haiku_power.c
		pvr_haiku_stubs.c
		pvr_haiku_sync.c
		pvr_haiku_vm.c

		# upstream/
		pvr_ccb.c
		pvr_cccb.c
		pvr_context.c
		pvr_device_info.c
		pvr_dump.c
		pvr_free_list.c
		pvr_fw.c
		pvr_fw_mips.c
		pvr_fw_startstop.c
		pvr_fw_trace.c
		pvr_fw_util.c
		pvr_hwrt.c
		pvr_job.c
		pvr_mmu.c
		pvr_queue.c
		pvr_stream.c
		pvr_stream_defs.c
		pvr_sync.c
		pvr_vm_mips.c
	SEARCH ${_sunxi_display} ${_compat} ${_glue} ${_upstream}
	INCLUDES
		${HAIKU_GPU_PRIVATE_HEADERS}/drivers
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics
		${HAIKU_GPU_PRIVATE_HEADERS}/graphics/powervr
		${_sunxi_display} ${_compat} ${_glue} ${_upstream}
	# Linux builds without these: the upstream code does arithmetic on void
	# pointers (pvr_device.h), compares mixed signs and passes int* for u32*.
	CFLAGS -Wno-pointer-arith -Wno-sign-compare -Wno-pointer-sign
	CXXFLAGS -fno-rtti
)
