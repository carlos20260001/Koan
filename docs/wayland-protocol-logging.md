# Wayland protocol logging

`app_server` can write the Wayland request/event handshake to a file when
`VOS_WAYLAND_PROTOCOL_LOG` is set in its environment. The value is the full
path to the log file; logging is disabled when it is unset.

For a QEMU guest using the repository's shared folder, first make sure the
mount is active and writable. If the mount service is installed:

```sh
sudo systemctl start vos-shared-mount.service
touch /home/vos-live/shared/wayland-write-test
rm /home/vos-live/shared/wayland-write-test
```

If that unit is not installed, mount the QEMU 9p share directly:

```sh
sudo mkdir -p /home/vos-live/shared
sudo mount -t 9p -o trans=virtio,version=9p2000.L \
  host_shared /home/vos-live/shared
```

Set the variable in `/etc/environment` so it is available to the login
session that starts `app_server`:

```text
VOS_WAYLAND_PROTOCOL_LOG=/home/vos-live/shared/wayland-protocol.log
```

Then log out and back in, or restart the guest session, and launch the
Wayland client. The log is appended to the host's `shared/wayland-protocol.log`.
Each entry includes the direction (`C->S` request or `S->C` event), client
credentials, protocol object, message, and decoded arguments. File-open errors
are reported to the app-server log; they do not prevent the desktop from
starting.

Protocol logging is intentionally opt-in because it records every Wayland
request and event and can grow quickly. It may include application window
titles and other protocol strings.
