<p align="center">
  <img src="docs/airos/airos-logo.png" alt="air/OS" width="280">
</p>

air/OS
=======================
**[Haiku upstream](https://www.haiku-os.org/)
| [Downloads](https://airos.works/)
| [Installer](docs/airos/INSTALLER.md)
| [Branding](docs/airos/BRANDING.md)
| [ROCK 5 ITX](docs/rock5-itx/README.md)
| [Raspberry Pi 4](docs/rpi4/STATUS.md)
| [Dragon Q8B](docs/dragon-q8b/STATUS.md)
| [X399 workstation](docs/x399-workstation/STATUS.md)**

air/OS is a fork of [Haiku](https://www.haiku-os.org/), the open-source
operating system for personal computing inspired by the BeOS. It keeps
Haiku's fast, simple and coherent desktop and adds what it needs to run on
ARM boards, laptops and workstations that Haiku does not support yet: new
drivers for GPUs, displays, video codecs, wireless, Bluetooth, USB and
power management, and a set of new system services and applications.

air/OS was forked from Haiku at `hrev60097` (R1/beta6 development) and
merges upstream deliberately; `master` currently carries upstream Haiku
through `hrev60207+70` (15 October 2026). The Haiku code, its authors and
its licenses are kept as they are, and *About this system* credits Haiku
next to air/OS. This is an independent, experimental and AI-assisted
project; it is not made or endorsed by Haiku, Inc., and upstream Haiku does
not accept AI-assisted contributions, so none of this work is submitted
there. Haiku® and the HAIKU logo® are registered trademarks of Haiku, Inc.

This branch (`airos-haiku-master`) is the Koan port of VitruvianOS onto the
air/OS tree: it adds code derived from
[VitruvianOS](https://github.com/VitruvianOS/Vitruvian) and
[Koan](https://github.com/carlos20260001/Koan) under the GPL-2.0; see the
appendix in `License.md` and the progress tracker in `PORTING.md`.

Supported machines
------------------
Each machine below runs air/OS natively in the lab. The linked status page
records what was verified on the hardware, with measurements; anything not
listed there should be treated as untested.

| Machine | SoC / platform | Image | What works | Status |
| --- | --- | --- | --- | --- |
| **Radxa ROCK 5 ITX** (reference ARM64 board) | Rockchip RK3588, 8 cores | `airos-arm64.iso` (UEFI), NVMe or eMMC install | PCIe + GICv3 ITS, NVMe, eMMC, USB 2, both 2.5 GbE ports, HDMI + DisplayPort, Mali-G610 GPU, VPU video decoding (H.264, HEVC, ten-bit HEVC, AV1), ES8316 audio, AX210 Wi-Fi 6 + Bluetooth | [STATUS](docs/rock5-itx/STATUS.md) |
| **Radxa Dragon Q8B** | Qualcomm SC8280XP, 8 cores | `airos-arm64.iso` (UEFI), NVMe install | NVMe, USB 3, SD card, 2 x 2.5 GbE, HDMI and both USB-C DisplayPort outputs, Adreno 690 (Vulkan 1.3, GLES 3.2, OpenGL), Iris video decode and encode, AX210 Wi-Fi + Bluetooth, temperature, power key, ADSP; audio is in progress | [STATUS](docs/dragon-q8b/STATUS.md) |
| **Raspberry Pi 4** | Broadcom BCM2711 | `airos-rpi4.img` (SD card) | SD with DMA, gigabit Ethernet, VL805 USB, V3D GPU (OpenGL/GLES, WebGL), both HDMI outputs, DSI panels as auxiliary displays, CYW43455 Wi-Fi and Bluetooth, H.264/HEVC decoding, GPIO | [STATUS](docs/rpi4/STATUS.md) |
| **Radxa Cubie A7S** | Allwinner A733 | `airos-cubie.img` (SD card) | SD, Ethernet, USB, USB-C DisplayPort (with USB PD), PowerVR GPU (Vulkan, GLES, WebGL 2), Cedar H.264/HEVC decoding, AIC8800 Wi-Fi (802.11ac), Bluetooth keyboards, mice and A2DP speakers, thermal management | [Wi-Fi](docs/cubie-a7s/WIFI.md), [thermal](docs/cubie-a7s/THERMAL.md), [Bluetooth audio](docs/cubie-a7s/BLUETOOTH-AUDIO.md) |
| **X399 workstation** | AMD Threadripper, X399 | `airos-x86_64.iso` | NVIDIA Pascal (GTX 1070/1080 Ti: Vulkan, OpenGL 4.5, NVDEC, NVENC) and AMD Radeon (WX 5100 on `amdgpu_drm`), several 4K monitors with per-monitor scaling, USB 3 at full speed, UVC webcams, MT7922 Wi-Fi + Bluetooth, SMB shares | [STATUS](docs/x399-workstation/STATUS.md) |
| **Framework Laptop 13** | AMD Ryzen 5 7640U, Radeon 760M | `airos-x86_64.iso` | `amdgpu_drm` graphics with panel brightness, I2C HID touchpad, keyboard and brightness/media keys, AX210 Wi-Fi + Bluetooth, speakers, lid sleep (suspend to idle), battery and AC policy | — |

Other x86_64 PCs with Haiku-supported hardware run the same x86_64 image.

What air/OS adds
----------------

### GPUs and displays
 * **Mali-G610 (RK3588):** `mali_csf`, a kernel driver for Arm's
   command-stream-frontend GPUs, with Mesa's Panfrost as the system OpenGL.
 * **Adreno 690 (SC8280XP):** the `adreno` driver with Mesa's Turnip
   (Vulkan 1.3) and zink (OpenGL), GPU recovery after faults, a clock that
   follows the load.
 * **PowerVR (A733):** `powervr` with Mesa's PowerVR Vulkan driver, transform
   feedback and WebGL 2 conformance fixes, an 800 MHz GPU clock.
 * **V3D (Raspberry Pi 4):** the `v3d` driver with Mesa's V3D/V3DV.
 * **AMD Radeon:** `amdgpu_drm`, Linux 6.18's amdgpu, DRM, TTM and Display
   Core ported to Haiku ([airos-works/linux-drm](https://github.com/airos-works/linux-drm)),
   with the `amdgpu_kms` accelerant, radeonsi (OpenGL/GLES/EGL), RADV
   (Vulkan 1.4), VA-API video decoding (`amdgpu_va`), GPU reset recovery and
   direct scanout.
 * **NVIDIA Pascal:** `nvidia_rm` (based on X547's NVIDIA port) with NVK and
   zink, NVDEC (H.264, H.265) and NVENC (H.264) media add-ons.
 * **Display drivers:** `rk3588_display`, `qcom_display` (HDMI through the
   board's bridge, USB-C DisplayPort with link training up to HBR3),
   `sunxi_display` (USB-C DisplayPort with USB PD), `rpi_display`, and
   `rpi_dsi` for small DSI panels through the new auxiliary display API
   (`BAuxDisplay`).
 * **app_server:** a `DisplayLayout` for several monitors in one desktop,
   per-monitor HiDPI and fractional scaling (100–250 %) with sharp text and
   even frames at 125–175 %, mirroring, windows and icons that follow
   monitors as they come and go, tear-free flushes at each output's vertical
   blank, and `B_WAIT_FOR_RETRACE` honoured on every driver with a blank
   source. See [SCALING](docs/airos/SCALING.md) and
   [RETRACE](docs/airos/RETRACE.md).

### Video and audio
 * Hardware video decoding through the Media Kit on every platform:
   `rk3588_vpu` (ROCK 5), `qcom_iris`/`qcom_video` (Dragon, also encoding),
   `sunxi_ve`/`sunxi_cedar` (Cubie), `rpi_hevc`/`rpi_mmal` (Pi 4), `nvdec`,
   `nvenc` and `amdgpu_va` (x86_64), each byte-exact against FFmpeg on its
   test vectors, with FFmpeg as the fallback.
 * Audio drivers: RK3588 (ES8316), BCM2835 (Pi HDMI/analog), Qualcomm
   AudioReach (in progress), USB Audio 2.0 with hot plug, A2DP Bluetooth
   audio output (`bluetooth_audio` media add-on).
 * The audio output and input are picked from the Deskbar volume control; a
   rebuilt Media preferences ([AUDIO-DEVICES](docs/airos/AUDIO-DEVICES.md)).
 * USB webcams (UVC) at 30 fps up to 1080p, through xHCI isochronous streams.
 * **MediaConverter**, rewritten around FFmpeg with hardware encoders
   (NVENC, AMD VCN, Iris), DVD and Blu-ray sources, subtitles and a command
   line ([MEDIACONVERTER](docs/airos/MEDIACONVERTER.md)).

### Wireless, Bluetooth and networking
 * A new **Wi-Fi preferences panel** and **WiFiStatus** Deskbar applet: one
   list of networks grouped by name and security, *Remember this network*,
   joining saved networks at boot (`wifiautojoin`).
 * Wi-Fi drivers: Intel AX210 (802.11ax) on ARM64 and x86_64, MediaTek
   MT7922 (`mt7922wifi`), AIC8800 (`aic8800wifi`, Cubie), Broadcom FullMAC
   (`broadcomfmac`, Pi 4 CYW43455), plus Haiku's other WLAN drivers built for
   ARM64; 802.11ac (VHT) in net80211.
 * **Bluetooth:** Low Energy scanning and connections, L2CAP fixed channels,
   ATT, SMP pairing and bonds, LE and classic HID keyboards and mice
   (`bluetooth_hid`, `bluetooth_le_mouse`), A2DP, media keys, a
   **BluetoothStatus** applet and reworked Bluetooth preferences.
   `bt_firmware` loads Intel and Realtek firmware, `h2generic` MediaTek's,
   `h4bcm` drives the Pi's UART controller.
 * Ethernet: `dwc_eqos` (RK3588), `tc956x` (Dragon), `bcm_genet` (Pi 4),
   `usb_rndis`, and `usb_ecm` at gigabit.
 * Network shares: SMB 2/3 through `smbfs` (libsmb2), mounted at boot by
   mount_server.

### Storage, USB and the kernel
 * ARM64: UEFI boot on RK3588, SC8280XP and A733, GICv3 ITS (MSI/MSI-X), SMMU
   domains, ASID, instruction cache and page-aging fixes, FDT max CPU
   frequencies.
 * New PCIe host bridges (`rk3588`, `qcom`, `bcm2711`), SD/eMMC hosts
   (`sdhci_fdt`, `sdhci_qcom`, `bcm2711_emmc2`, `sunxi_mmc`), EHCI and xHCI
   on FDT, DWC3, NVMe with MSI-X and TRIM.
 * USB: hot plug and SuperSpeed bursts on xHCI, USB 3 disks at full speed,
   faster enumeration that no longer holds up other buses, ACPI port reset,
   suspend to idle with devices attached, fixes for hub and device removal.
 * Power: `power_daemon` policy for AC/battery and the lid, Power
   preferences, suspend to idle on AMD laptops, thermal drivers
   (`sunxi_thermal` with CPU frequency control, `qcom_thermal`,
   `rpi_thermal`), PMIC and power keys, faster x86 boots
   ([BOOT-TIME](docs/airos/BOOT-TIME.md)).
 * `kdl_capture` records kernel debugger sessions to a file.

### Desktop and applications
 * **Terminal** rebuilt on a new engine with kitty's protocols: graphics,
   ligatures, color emoji, shell integration, clickable links, file
   transfer and drag and drop ([TERMINAL](docs/airos/TERMINAL.md)).
 * **Quick Look:** the space bar in Tracker previews files
   ([QUICK-LOOK](docs/airos/QUICK-LOOK.md)).
 * An on-screen volume and brightness panel (`osd_server`), Deskbar window
   icons, air/OS's Deskbar menu with Office, Media and Utilities folders
   ([DESKBAR-MENU](docs/airos/DESKBAR-MENU.md)).
 * `mcp_server`: an optional on-device [MCP](docs/airos/MCP.md) server (off
   by default, MCP preferences) for remote diagnostics, testing and driver
   deployment.
 * The **Installer** sets up a whole disk with an EFI partition
   ([INSTALLER](docs/airos/INSTALLER.md)); live images boot read-only media
   safely.
 * The air/OS look: icons, Deskbar logo, boot screen, desktop and volume
   artwork, generated from one source ([BRANDING](docs/airos/BRANDING.md)).
 * The images also carry the owner's applications, built by CI: the Summit
   web browser (WebKit, GPU compositing, WebGL), airTime (video player with
   hardware decoding), airShot, AirTop, AirPins (Pi GPIO), Amp, Burrow,
   Clipper, Kiri, LCDMonitor, Natter, TurboChook and others; Aurora runs
   Electron applications, including Code - OSS.

Getting air/OS
--------------
Every push to `master` and a nightly run build four images on the air/OS
build server (`.github/workflows/airos-images.yml`, scripts in
[airos-works/airos-ci](https://github.com/airos-works/airos-ci)); each is smoke
tested and published as a GitHub prerelease (`image-<target>-*`) and on
[airos.works](https://airos.works/):

| Image | For |
| --- | --- |
| `airos-x86_64.iso` | PCs, EFI and BIOS (USB stick or CD) |
| `airos-arm64.iso` | UEFI ARM64 machines: ROCK 5 ITX, Dragon Q8B |
| `airos-rpi4.img` | Raspberry Pi 4 SD card, boots directly |
| `airos-cubie.img` | Radxa Cubie A7S SD card (Radxa boot chain, air/OS U-Boot) |

The ISOs boot to the desktop and the Installer.

Write every image raw to the whole USB stick or SD card: `dd` on Linux and
macOS, Rufus in DD Image mode on Windows. Don't use balenaEtcher: 2.1.4
silently dropped 2 MB from the x86-64 ISO while writing a USB stick, which
then stopped at "No boot volume found"; the same file via `dd` booted.
[Writing an image](https://airos.works/downloads/write/) has the commands
and how to check the written copy against the build manifest.

Building air/OS
---------------
air/OS builds like Haiku: see [ReadMe.Compiling.md](ReadMe.Compiling.md)
for the host tools and the cross-toolchain (`configure
--build-cross-tools x86_64` or `arm64`). The image profiles are defined in
build configurations under `tools/`; include one from the build directory's
`UserBuildConfig`:

| Target | Configuration | Command |
| --- | --- | --- |
| x86_64 release ISO | `tools/airos/ci/UserBuildConfig` | `jam -q @nightly-airos-x86_64` |
| ARM64 UEFI ISO | `tools/airos/ci/UserBuildConfig` | `jam -q @airos-arm64` |
| Raspberry Pi 4 SD | `tools/airos/ci/UserBuildConfig` | `jam -q @airos-rpi4 airos-rpi-image` |
| Cubie A7S SD | `tools/airos/ci/UserBuildConfig` | `jam -q @airos-cubie`, then `tools/cubie-a7s/build-sd-image.sh` |
| ROCK 5 lab image | `tools/rock5-itx/UserBuildConfig` | `jam -q @rock5full-mmc` |
| Pi 4 / Cubie lab images | `tools/rpi4`, `tools/cubie-a7s` | `@minimum-rpi4`, `@minimum-cubie` |

The release configuration expects the packages CI builds (the
applications, Summit, Mesa and the other libraries from the airos-works forks,
firmware); its header lists the variables that point at them. The lab
configurations add a remote shell, probes and diagnostics for the hardware
lab and must not be used for public images. Third-party code (Mesa,
FFmpeg, MPP, WebKit, linux-drm, firmware …) lives in forks under
[airos-works](https://github.com/airos-works) on an `airos` branch.

Per-machine build, install and recovery notes:
[ROCK 5 ITX](docs/rock5-itx/README.md) (cross-toolchain, NVMe/eMMC
install, the remote lab), [Raspberry Pi 4](docs/rpi4/SETUP.md),
[X399 workstation](docs/x399-workstation/README.md) and
[AMD GPUs](docs/x399-workstation/AMDGPU-DRM.md).

Development
-----------
`master` is the integration branch and holds all of the fork's work. New
work goes on a short topic branch from `master` and is merged back with
`git merge --no-ff`; device bring-up uses a branch per board
(`rpi4`, `cubie-a7s`, `dragon-q8b`). Upstream Haiku is merged on a topic
branch and build-checked on both architectures first. Every merge into
`master` rebuilds the public images, so build x86_64 and ARM64 before
pushing. [AGENTS.md](AGENTS.md) has the working rules.

Haiku
-----
Haiku is an open-source operating system that specifically targets personal
computing. Inspired by the BeOS, Haiku is fast, simple to use, easy to learn
and yet very powerful. Its goals, which air/OS shares:

 * Sensible defaults with minimal configuration required.
 * Clean, clear, concise code.
 * Unified desktop environment.

To use or contribute to Haiku itself, go to the
[Haiku website](https://www.haiku-os.org/),
[mailing lists](https://www.haiku-os.org/community/ml),
[issue tracker](https://dev.haiku-os.org/) and
[API docs](https://api.haiku-os.org). Haiku provides
[nightly images](http://download.haiku-os.org/) and
[release images](https://www.haiku-os.org/get-haiku), and its
[patch guidelines](https://dev.haiku-os.org/wiki/CodingGuidelines/SubmittingPatches)
apply to contributions made there, not here.
