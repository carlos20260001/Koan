# media_controls_server: air/OS's "now playing" service: players register
# through libmedia_controls, the server routes media keys and the Deskbar
# replicant's actions to the active one. Works on Linux as it is (BMessages).
# TODO(linux backend): an MPRIS bridge on the session bus, publishing the
# active player as org.mpris.MediaPlayer2.* and listing native Linux MPRIS
# players (browsers, Wayland clients) next to air/OS's.
Server(
	media_controls_server

	SOURCES
	MediaControlsServer.cpp
	MediaControlsView.cpp
	MediaIcons.cpp
	NowPlayingWindow.cpp

	LIBS localestub
	RDEF media_controls_server.rdef
)

VosPrivateHeaders(media_controls_server app interface media_controls shared)
DoCatalogs("x-vnd.vos-media_controls_server" servers/media_controls)
