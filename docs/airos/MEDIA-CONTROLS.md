# Media controls ("now playing")

Applications that play media register a *player* with the system; the
keyboard's media keys (play/pause, next, previous, stop) then reach the
player that is active, whatever window is in front, and a Deskbar replicant
shows what is playing and controls it. This is the role MPRIS has on Linux
and Now Playing on macOS, and what a browser's Media Session API maps to
(airos-works/summit#144). Since branch `media-session` (haiku#55,
2026-11-02).

## What the user sees

- While a player is registered, a note icon sits in the Deskbar tray (dimmed
  while nothing plays). Its tool tip names the title, artist and state.
- A click on it opens a small panel under (or above) it: the artwork, the
  title, artist and album, the application, the elapsed time over a bar
  (a click on the bar seeks when the player can), and previous, play/pause
  and next buttons. The panel closes when it loses the focus or with Escape.
- The right mouse button opens a menu: Play/Pause, Previous, Next, Stop, the
  list of players when there are several (the marked one gets the keys;
  choosing another makes it active), and *Show <application>*.
- The middle mouse button toggles play/pause.
- The media keys go to the active player. The volume keys are unchanged
  (`setvolume`, osd_server).
- The icon goes away about 1.5 s after the last player.

The active player is the one the user chose in the menu, else the one that
started playing last among those playing, else the one that played last,
else the one that registered last. A player whose application quits (or
crashes) goes away.

## How it is built

```
headers/private/media_controls/MediaControls.h  the message protocol (stable)
headers/private/media_controls/MediaSession.h   BPrivate::MediaSession, the client class
src/kits/media_controls/                         libmedia_controls.a (in haiku_devel)
src/servers/media_controls/                      media_controls_server + its Deskbar replicant
src/bin/mediakey.cpp                             the media keys' command
src/tests/kits/media_controls/                   media_session_test
```

- **media_controls_server** (`application/x-vnd.vos-media_controls_server`,
  `/system/servers`) is an `on_demand` service of the launch daemon
  (`data/launch/system`): it starts with the first player's registration. It
  is its own small server and not part of media_server: players need not use
  the Media Kit at all (Summit plays through GStreamer/WebKit, Electron apps
  through Chromium), and a restart of the media_server (device changes) must
  not drop the players. It keeps the players, picks the active one, forwards
  actions, tells watchers (the replicant, its panel) about every change, and
  adds/removes its Deskbar replicant (from a looper of its own, so that it
  never waits for the Deskbar while the replicant waits for it).
- **The replicant** lives in the server's executable
  (`instantiate_deskbar_item`, added with `BDeskbar::AddItem(entry_ref*)`
  like mail_daemon's). It only talks to the server asynchronously; a
  replicant the Deskbar restores after a reboot removes itself when the
  server is not running.
- **Media keys**: the keyboard drivers turn the keys into consumer-page key
  codes (USB/Bluetooth HID usages `0xc00cd` play/pause, `0xc00b0` play,
  `0xc00b1` pause, `0xc00b7` stop, `0xc00b5` next, `0xc00b6` previous); the
  Shortcuts preferences (input_server's shortcut_catcher filter) bind them
  to `/bin/mediakey <key>` by default. `mediakey` gives the key to the media
  controls' active player first, and only when there is none (or it does not
  take the key) falls back to the older `MediaKeys.h` query of all running
  applications (Amp, airTime and MediaPlayer still answer it). So the keys
  work regardless of the focused window, through the path that already
  existed; no new input_server add-on is needed. A PS/2 (and Hyper-V)
  keyboard's Play/Pause key (`E0 22`) now gives play/pause (`0xc00cd`) as
  it should, not play (`0xc00b0`), so it toggles.
- `mediakey status` lists the players, the active one first, with state,
  position, metadata and actions; `mediakey seek <seconds>` seeks the active
  player; `mediakey -v <key>` says where a key went.

## Using it in an application (C++)

```cpp
#include <MediaSession.h>		// UsePrivateHeaders media_controls
								// link libmedia_controls.a

MediaSession fSession;			// e.g. a member of the player window

// when something is loaded:
fSession.SetMetadata(title, artist, album, artworkBitmap);	// any may be NULL
fSession.SetSupportedActions(B_MEDIA_DEFAULT_ACTIONS
	| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_NEXT)
	| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_PREVIOUS)
	| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_SEEK));
fSession.SetPlaybackState(B_MEDIA_SESSION_PAUSED, position, duration);
fSession.Register(this);		// this window gets the actions

// on every play/pause/stop and after a seek (not continuously: the
// position is extrapolated from position, its time and the rate):
fSession.SetPlaybackState(B_MEDIA_SESSION_PLAYING, position, duration);

// in MessageReceived():
case kMsgMediaSessionAction:
{
	media_session_action action;
	bigtime_t position;
	if (fSession.ParseAction(message, &action, &position) == B_OK)
		...		// play, pause, toggle, stop, next, previous, seek
	break;
}
```

All calls are asynchronous and never wait for the server; the object keeps
the whole state and registers again if the server was restarted. The
destructor (or `Unregister()`) takes the player away. Artwork is copied and
scaled down to 256 × 256. An application with several players (windows,
tabs) uses one `MediaSession` each.

**MediaPlayer** is the first client (`src/apps/mediaplayer/MainWin.cpp`):
each window with a file registers; title, artist and album come from the
file's tags (container metadata), else its `Audio:Title`/`Audio:Artist`/
`Audio:Album` attributes, else the file name; the artwork is a
`cover`/`folder`/`front`/`albumart`/`album` `.jpg`/`.png`/`.webp` next to
an audio file (embedded ID3/MP4 pictures are not exposed by the FFmpeg
reader yet). Next/previous are offered when the playlist has more than one
entry, seek always.

## Using it without the class (airTime, Summit, other languages)

The protocol in `MediaControls.h` is plain BMessages to the server's
signature, so anything that can send a BMessage can be a player:

1. Send `kMsgMediaSessionRegister` with an `int64 "session"` you choose
   (unique in your team; `team << 32 | n` works), a `messenger "target"`,
   optionally `"name"`, and the fields of the groups below.
2. Send `kMsgMediaSessionUpdate` with `"session"` and one or more groups:
   *metadata* (`"title"` marks it; `"artist"`, `"album"`, `"artwork"` = an
   archived BBitmap), *playback* (`int32 "state"` marks it: 0 stopped,
   1 playing, 2 paused; `int64 "position"`, `"position_time"` =
   `system_time()` of that position, `"duration"`, `float "rate"`) and
   `uint32 "actions"`. A group replaces all of that group.
3. Handle `kMsgMediaSessionAction` (`int32 "action"`, `int64 "position"` for
   seek) at the target.
4. Send `kMsgMediaSessionUnregister` when done (a quitting team is dropped
   anyway).

- **airTime** (apps/airTime) would register one session per player window in
  its engine controller next to its existing `MediaKeys.h` handler; chapters
  map naturally to next/previous, and its poster frame or the file's cover
  art to the artwork.
- **Summit** (Media Session API, airos-works/summit#144): one `MediaSession`
  per page that has an active `navigator.mediaSession` (or an audible media
  element). `MediaMetadata` title/artist/album/artwork go to `SetMetadata()`
  (decode the largest artwork image to a BBitmap),
  `setPositionState({duration, playbackRate, position})` and the media
  element's play/pause events to `SetPlaybackState()`, and the page's
  `setActionHandler()` calls decide `SetSupportedActions()`: play, pause,
  stop, previoustrack, nexttrack, seekto (seekbackward/seekforward have no
  key here). Incoming actions are dispatched to the page's handlers (or to
  the media element's play()/pause() when it set none). The header and
  libmedia_controls.a come with haiku_devel.

## Testing

- `media_session_test` (`jam media_session_test`, in `src/tests/kits/
  media_controls`): registers two players and a child process with the
  running server and checks registration, metadata, active player choice
  (playing, played last, registered last, user choice, app death), action
  routing (toggle mapped to play/pause for a player without toggle,
  unsupported actions not handled, seek position) and artwork scaling.
  Run it with no other player registered; it prints PASS/FAIL per check and
  exits 0 when all pass.
- In QEMU, `vm.py key DIR audioplay` (also `audionext`, `audioprev`,
  `audiostop`) presses the media keys on the emulated PS/2 keyboard. QEMU's
  USB keyboard (`usb-kbd`) has no media keys, so the VM must run without it
  (or the PS/2 keyboard must have the input focus) for that test.

- Trying a new server build on a running system: kill the old server and
  start the new one from a *new* file. The Deskbar has the server's
  executable loaded as the replicant's add-on; overwriting that file in
  place corrupts the Deskbar's mapped code and crashes it. (A restarted
  server removes the old replicant and adds its own with the next player
  update.)

## Not done yet

- Embedded cover art (ID3 APIC, MP4 `covr`) in MediaPlayer: the FFmpeg
  reader does not hand out attached pictures.
- airTime, Amp and Summit are not clients yet (they still answer the
  `MediaKeys.h` query, so the keys reach them when no registered player
  takes them).
- No lock-screen or notification view; the replicant is the only UI.
