#!/usr/bin/env bash
# USB audio on QEMU: boots a lab image (it needs the lab overlay's telnet
# login, cubie_fetch and cubie_audio_test) on QEMU's virt machine with an
# xHCI controller and QEMU's usb-audio device (a full-speed USB Audio Class 1
# speaker, 48 kHz stereo 16 bit), which records what it is sent into a WAV
# file, and a display (U-Boot's EFI frame buffer on bochs-display) so that
# app_server, and with it the media add-on server, runs. It plays a tone with cubie_audio_test straight through usb_audio,
# then a 44.1 kHz WAV through the Media Kit (media_client, the system mixer,
# the hmulti add-on), and checks both tones in the recording.
#
#   qemu-usb-audio.sh [bfs image] [output directory]
#
# This covers usb_audio, the multi_audio interface and the hmulti add-on, not
# the board's EHCI: QEMU's EHCI does not do split transactions and its usb-hub
# is full speed, so a QEMU guest has no siTD path.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD=${CUBIE_BUILD:-/mnt/HaikuWork/cubie/build}
IMAGE=${1:-$BUILD/haiku-cubie-airos.image}
OUT=${2:-$(mktemp -d "${TMPDIR:-/mnt/HaikuWork/tmp}/cubie-qemu-audio.XXXXXX")}
QEMU=${QEMU:-/mnt/HaikuWork/build/qemu-10.2.0/qemu-system-aarch64}
UBOOT=${QEMU_UBOOT:-/mnt/HaikuWork/cubie/downloads/u-boot-qemu/x/usr/lib/u-boot/qemu_arm64/u-boot.bin}
SHELL_PY=/mnt/HaikuWork/rpi4/haiku/tools/rpi4/shell.py
PORT=${QEMU_TELNET_PORT:-2323}
TONE=${TONE:-1000}
MEDIA_TONE=${MEDIA_TONE:-440}
mkdir -p "$OUT"

# the Media Kit's tone: 5 s at 44.1 kHz, which the mixer resamples to 48
python3 - "$OUT/tone-media.wav" "$MEDIA_TONE" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1], "wb")
w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
tone = float(sys.argv[2])
w.writeframes(b"".join(struct.pack("<hh", *(2 * [int(16000 * math.sin(
	2 * math.pi * tone * i / 44100))])) for i in range(5 * 44100)))
w.close()
PY

WORK=$(mktemp -d "${TMPDIR:-/mnt/HaikuWork/tmp}/cubie-qemu.XXXXXX")
pid=
trap 'kill $pid 2>/dev/null || true; rm -rf "$WORK"' EXIT
"$HERE/build-sd-image.sh" "$IMAGE" "$WORK/sd.img" >/dev/null
LOG=$OUT/serial.log
WAV=$OUT/usb-audio.wav
"$QEMU" -M virt,gic-version=3 -cpu cortex-a76 -smp 4 -m 4096 -bios "$UBOOT" \
	-drive if=none,file="$WORK/sd.img",format=raw,id=hd0 \
	-device virtio-blk-device,drive=hd0 -nographic -serial mon:stdio \
	-device virtio-net-device,netdev=n0 \
	-netdev user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:23 \
	-audiodev wav,id=snd0,path="$WAV",out.frequency=48000,out.channels=2,out.format=s16 \
	-device qemu-xhci,id=xhci -device usb-audio,bus=xhci.0,audiodev=snd0 \
	-device bochs-display \
	</dev/null > "$LOG" 2>&1 &
pid=$!

guest() {
	env RPI4_STATE="${CUBIE_STATE:-/mnt/HaikuWork/cubie/state}" \
		RPI4_TELNET_PORT=$PORT python3 "$SHELL_PY" 127.0.0.1 "$1" "${2:-60}"
}

deadline=$((SECONDS + 300))
until guest "echo up" 10 >/dev/null 2>&1; do
	if (( SECONDS > deadline )) || ! kill -0 $pid 2>/dev/null \
		|| grep -a -q -E "PANIC|Kernel Debugging Land" "$LOG"; then
		echo "the guest did not come up, see $LOG" >&2
		exit 1
	fi
	sleep 5
done
sleep 20

# one connection's worth of a file for cubie_fetch (the host is 10.0.2.2)
serve() {
	python3 -c '
import socket, sys
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", int(sys.argv[1]))); s.listen(1)
c, _ = s.accept(); c.sendall(open(sys.argv[2], "rb").read()); c.close()
' "$1" "$2"
}

# launch_daemon restarts a killed media_server; stop it through the roster,
# then its add-on server, which holds the device
stop_media='launch_roster stop x-vnd.vos-media_server; sleep 3; for team in $(ps | grep -a media_addon_server | grep -v grep | awk "{ print \$2 }"); do kill $team; done; sleep 2'

{
	echo "== listusb"; guest "listusb"
	echo "== /dev/audio"; guest "ls -R /dev/audio"
	echo "== stop the media services (the driver takes one opener)"
	guest "$stop_media; ps | grep -a media_ | grep -v grep; true"
	echo "== cubie_audio_test, 48 kHz, $TONE Hz"
	guest "cubie_audio_test -s 6 -f $TONE" 60 || true
	sleep 3
	echo "== the Media Kit: $MEDIA_TONE Hz at 44.1 kHz through media_client"
	serve $((PORT + 1)) "$OUT/tone-media.wav" &
	server=$!
	guest "cubie_fetch 10.0.2.2 $((PORT + 1)) /boot/home/tone-media.wav; ls -l /boot/home/tone-media.wav" 60
	wait $server || true
	guest "launch_roster start x-vnd.vos-media_server; sleep 15; ps | grep -a media_ | grep -v grep" 60
	echo "-- the hmulti add-on has the device open now:"
	guest "cubie_audio_test -s 1; true" 30
	guest "media_client play /boot/home/tone-media.wav 2>&1 | tail -5" 90 || true
	echo "== syslog"
	guest "grep -a -E 'usb_audio|xhci.*(error|isoch)|PANIC' /var/log/syslog | tail -40" 60
} 2>&1 | tee "$OUT/guest.log"

kill $pid 2>/dev/null || true
wait $pid 2>/dev/null || true

python3 - "$WAV" "$TONE" "$MEDIA_TONE" <<'PY' | tee "$OUT/wav-check.log"
# The WAV QEMU recorded (only while the device streams, so the tones follow
# each other without the pauses between them): 20 ms windows, each loud one
# classified by its zero crossings; per tone, how long it played and any
# dropout (2 ms or more of near silence) inside its stretches.
import struct, sys, wave
path, tones = sys.argv[1], [float(t) for t in sys.argv[2:]]
w = wave.open(path)
rate, channels = w.getframerate(), w.getnchannels()
data = w.readframes(w.getnframes())
left = struct.unpack("<%dh" % (len(data) // 2), data)[::channels]
print("wav: %d Hz, %d channels, %.2f s" % (rate, channels, len(left) / rate))

size = rate // 50
labels = []
for start in range(0, len(left) - size + 1, size):
	window = left[start:start + size]
	if max(abs(v) for v in window) < 2000:
		labels.append(None)
		continue
	crossings = sum(1 for a, b in zip(window, window[1:]) if a < 0 <= b)
	frequency = crossings * rate / size
	match = [t for t in tones if abs(frequency - t) <= t * 0.05 + 50]
	labels.append(match[0] if match else frequency)

ok = True
for tone in tones:
	windows = [i for i, label in enumerate(labels) if label == tone]
	# dropouts inside runs of this tone's windows, not at their ends
	gaps = 0
	for i in windows:
		if labels[i - 1:i] != [tone] or labels[i + 1:i + 2] != [tone]:
			continue
		run = 0
		for v in left[i * size:(i + 1) * size]:
			run = run + 1 if abs(v) < 500 else 0
			if run == rate // 500:
				gaps += 1
	print("%.0f Hz: %.2f s, %d dropouts" % (tone, len(windows) * size / rate,
		gaps))
	ok = ok and len(windows) * size / rate >= 3 and gaps == 0
other = sorted(set(round(l) for l in labels if l is not None and l not in tones))
if other:
	print("other frequencies: %s" % other[:20])
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
PY
