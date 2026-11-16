#!/bin/sh


# Send a message to and wait for a reply from all servers to determine when
# everything's ready.
SIGNATURES="
	application/x-vnd.vos-registrar
	application/x-vnd.vos-mount_server
	application/x-vnd.vos-powermanagement
	application/x-vnd.vos-cddb_daemon
	application/x-vnd.vos-midi_server
	application/x-vnd.vos-net_server
	application/x-vnd.vos-debug_server
	application/x-vnd.Be-PSRV
	application/x-vnd.vos-package_daemon
	application/x-vnd.vos-notification_server
	application/x-vnd.Be-input_server
	application/x-vnd.Be.media-server
	application/x-vnd.Be.addon-host
	application/x-vnd.Be-TRAK
	application/x-vnd.Be-TSKB"

for SIGNATURE in $SIGNATURES
do
	waitfor -m $SIGNATURE
	hey -s $SIGNATURE get
	if [ $? -ne 0 ]
	then
		echo "Failed to get a reply for $SIGNATURE"
		exit 1
	fi
done

system_time
