# air/OS boot splash (src/system/boot_splash, PORTING.md M7): the static
# vos-splash, its initramfs-tools hook and scripts, and the systemd units for
# the stages after the initramfs. No Plymouth.
#
# Program and initramfs pieces go in the runtime (vos) package: its
# "activate-noawait update-initramfs" trigger rebuilds the initramfs with
# them. Units and drop-ins go with the other units in the data package.
if(TARGET vos-splash) # ported subset: only where the target is configured
ImageInclude("/usr/libexec" vos-splash)

set(_vos_splash_data "${CMAKE_SOURCE_DIR}/data/boot_splash")
install(PROGRAMS "${_vos_splash_data}/initramfs-tools/hooks/vos-splash"
	DESTINATION /usr/share/initramfs-tools/hooks/ COMPONENT runtime)
install(PROGRAMS "${_vos_splash_data}/initramfs-tools/scripts/init-top/vos-splash"
	DESTINATION /usr/share/initramfs-tools/scripts/init-top/ COMPONENT runtime)
install(PROGRAMS "${_vos_splash_data}/initramfs-tools/scripts/init-bottom/vos-splash"
	DESTINATION /usr/share/initramfs-tools/scripts/init-bottom/ COMPONENT runtime)

# The units, and drop-ins that order the stages and pull them in: nothing
# needs enabling. Drop-ins for units this image lacks (registrar, app_server,
# janus) are inert.
install(DIRECTORY "${_vos_splash_data}/systemd/"
	DESTINATION /etc/systemd/system/ COMPONENT data)
endif()
