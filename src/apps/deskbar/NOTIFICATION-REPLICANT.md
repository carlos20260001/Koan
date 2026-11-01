# Notification Deskbar Replicant

The applet installs an archiveable button in Deskbar. Clicking it shows the
existing notification window, and clicking it again while that window is active
hides it.

The button communicates with `notification_server` through Koan's private
Be-message protocol. The server also exposes the core
`org.freedesktop.Notifications` D-Bus methods: `Notify`, `CloseNotification`,
`GetCapabilities`, and `GetServerInformation`, plus the `NotificationClosed`
signal. Its only advertised capability is `body`.

Actions, icons, hints, markup, and persistent notification history are not
implemented. A notification expiration of `-1` uses the notification
server's configured timeout; `0` disables expiration.
