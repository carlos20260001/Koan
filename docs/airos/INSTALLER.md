# air/OS installer and ISO

> **Note (2026-11-16, Linux port):** this page describes the air/OS
> Installer on the Haiku kernel. On this tree (air/OS on Linux, see
> `PORTING.md`, milestone M8) DriveSetup and the Installer are
> VitruvianOS's: they plan partition changes in the storage kit
> (`src/kits/storage/disk_device/PartitionPlanBuilder.cpp`) and apply
> them, the copy and the GRUB bootloader through the root helper
> `data/libexec/vos-install-helper` under polkit. The whole-disk UEFI
> setup, BFS target, `haiku_loader.efi` copy and Jam ISO build below do
> not apply here. The helper's behaviour is exercised by
> `src/apps/installer/tests/sim/sim-install.sh`.

air/OS installs onto a whole disk in a few clicks: the Installer erases the
disk, sets it up to start with UEFI and installs. None of the manual steps of
Haiku's [UEFI booting guide](https://www.haiku-os.org/guides/uefi_booting/)
are needed. This page covers the Installer, the ISO that carries it, how to
build that ISO, and what was tested.

## Installing onto a whole disk

The first boot prompt says *Welcome to air/OS!* and offers *Install air/OS*.
The Installer opens with a short notice (air/OS is beta software, back up
first; a whole disk is erased, a prepared partition is kept), then its main
window. Its *Onto:* menu has two parts:

- **Erase a whole disk and set it up for UEFI:** every disk, named by its
  driver (USB disks, and now NVMe drives, report their model) or by its bus,
  with its size, its path and the volumes on it. The disk the system runs
  from, the disk holding the source, read-only disks and disks too small for
  the installation are listed but disabled, with the reason.
- **Install onto an existing partition:** the partitions, as before.

Choosing a disk and pressing *Begin* asks once more, naming the disk, its path
and its volumes; *Cancel* is the default button, so Enter does not erase
anything. Then the Installer (`WorkerThread::_PrepareWholeDisk()`):

1. unmounts every volume on the disk (forcibly if one is busy: the user has
   agreed to erase it);
2. writes an empty GUID partition map;
3. creates a 256 MiB *EFI system partition* and one *Be File System*
   partition over the rest, both aligned to 1 MiB;
4. formats them: FAT32 named `EFI`, BFS named `airOS` (2 KiB blocks);
5. installs onto the BFS partition as for any partition;
6. copies the system's EFI loader (`/boot/system/data/platform_loaders/
   haiku_loader.efi`) to the removable-media path of the EFI system partition,
   `EFI/BOOT/BOOTAA64.EFI` (`BOOTX64.EFI` on x86_64). UEFI firmware starts that
   from a disk without a boot entry, so no NVRAM variable is needed;
7. writes everything out (`sync`) before it reports success.

Each partitioning step is written on its own and retried on a freshly read
disk if the kernel reports a stale change counter or a busy disk. A failure
names the step that failed, in the alert and in the syslog.

Disk changes go through the storage kit exactly as DriveSetup's do. Two kit
and kernel details had to be fixed for it: `BMutablePartition::
UninitializeContents()` dereferenced the parent of a whole disk (DriveSetup
never uninitializes a disk), and the kernel refuses to create a partition
without a parameter string.

### After installing

Remove the installation medium (on the NanoKVM: unmount the image) and
restart. The disk starts on any UEFI firmware that tries the removable-media
path of its disks. Firmware that only follows its own boot entries needs the
disk moved up in its boot order once: a boot entry that names a partition by
its GUID (like the ROCK 5's old *Haiku NVMe* entry) stops matching when the
disk gets a new partition map. The ROCK 5 is set up accordingly, see below.

## The live medium

`airos-arm64.iso` is a hybrid image made by `anyboot`: an ISO 9660 volume with
an El Torito UEFI entry, and an MBR with the BFS system (type `0xEB`) and an
EFI system partition (`0xEF`). It starts from a CD, a USB stick it was written
to, or the NanoKVM's virtual drive in either mode.

A hybrid ISO is always used live: the kernel mounts its BFS read-only beneath
a write overlay (`vfs_boot.cpp`, `is_hybrid_iso_partition()`), also when it is
not on a CD. Haiku's `usb_disk` does not ask a disk whether it is write
protected (MODE SENSE is disabled upstream because some devices stall on it),
so a write-protected USB disk such as the NanoKVM's read-only virtual drive
used to be mounted writable, and the kernel panicked minutes later when the
journal could not be written. A writable stick is not changed by booting it
either. Settings made in the live session (the language chosen at the first
boot prompt, for one) are copied to the installed system. Kernel drivers'
start markers (`home/config/settings/kernel/drivers/*.starting`, see
amdgpu_drm) are not: they describe a start in the live session, and on the
installed system one would keep the driver off.

The first boot prompt and the Installer now get the user environment
(`data/launch/user`): without it ICU found no data on arm64, so the language
list was empty and sizes printed without numbers.

## What the ISO contains

The full regular image for the ROCK 5 ITX (every in-tree application,
preference and demo) built from branch `airos-release` (retired on
2026-11-05; CI now builds every air/OS image from `master`) with
`--distro-compatibility compatible`:

- the ROCK 5 drivers (display, Mali GPU, VPU, audio, NVMe, PCIe, eMMC, USB,
  RTL8125, Wi-Fi and Bluetooth) with their settings files, the fork's Mesa and
  the Mali firmware;
- the X399 workstation branch merged in: its architecture-independent work
  (MT7922/MediaTek Bluetooth, the Bluetooth LE stack, display layout and
  scaling, network shares with smbfs, usb_raw and NVMe fixes) is in this
  binary; its x86-only drivers (NVIDIA among them) are in the source tree
  but naturally not in an arm64 image;
- the air/OS branding;
- current builds of all applications in `/mnt/HaikuWork/apps` that run on
  arm64: airShot, Amp, Burrow, Kiri, LCDMonitor, Natter, Turbo Chook and
  **Summit**, the default web browser (its engine is the `summit_webkit`
  package, in `/boot/system/lib/summit-webkit`). Aurora cannot run on arm64:
  it needs Node.js, and there is no arm64 Haiku build of it;
- libcurl with OpenSSL (the bootstrap one had no TLS) and CA certificates.

Summit is the preferred application for `text/html`, `application/xhtml+xml`,
`image/svg+xml`, `multipart/related` and http(s) URLs in the system MIME
database (`src/data/mime_db`).

## Building the ISO

```sh
# a build directory for the release, with the ROCK 5 cross tools
mkdir -p /mnt/HaikuWork/airos/build-release && cd /mnt/HaikuWork/airos/build-release
../release/configure --distro-compatibility compatible \
	--cross-tools-prefix /mnt/HaikuWork/build/arm64/cross-tools-arm64/bin/aarch64-unknown-haiku-
echo 'include /mnt/HaikuWork/airos/release/tools/rock5-itx/UserBuildConfig ;' > UserBuildConfig

# the applications and Summit as arm64 packages -> /mnt/HaikuWork/airos/packages-arm64
bash ../release/tools/airos/build-arm64-app-packages.sh

# the ISO -> airos-arm64.iso
jam -q -j10 @airos-anyboot
```

The `airos-anyboot` profile (`tools/rock5-itx/UserBuildConfig`) is the
`rock5full-mmc` image without the lab's telnet shell and probes, plus every
package in `/mnt/HaikuWork/airos/packages-arm64`, the libraries in
`/mnt/HaikuWork/airos/lib-arm64` and the CA certificates. Anyboot images used
to be x86-only (BIOS MBR code and El Torito floppy); on EFI-only platforms
both are now left out and `anyboot` writes the MBR signature itself.

Summit and its engine are built from `/mnt/HaikuWork/build/summit-arm64`
(see its `README.md`): the WebKit tree there is upstream WebKit plus Summit's
port patch and a few arm64 changes, built by `run-engine-build.sh`.
`build-arm64-app-packages.sh summit_webkit summit` packages the engine and
builds the browser from the Summit checkout the engine was built with. Every
engine library gets an `$ORIGIN` RPATH (Haiku resolves a library's
dependencies with that library's own RPATH only), using patchelf from
`/mnt/HaikuWork/toolchains/patchelf`.

## Testing in QEMU

`tools/airos/qemu-install-test.sh DIR ISO 0|1` boots the ISO in arm64 QEMU,
goes through the first boot prompt and the Installer with the mouse, and
erases and installs onto a blank NVMe disk (0) or one with an MBR, a FAT32
volume and two more partitions (1). `tools/airos/qemu_vm.py` drives QEMU by
hand (screenshots, clicks, keys) and boots the installed disk alone.

Results on 2 October 2026, all with the ISO as a read-only USB disk unless
noted:

| Test | Result |
| --- | --- |
| install onto the blank disk | installs; the disk has a GPT, a 256 MiB FAT32 ESP with `EFI/BOOT/BOOTAA64.EFI` and the `airOS` BFS partition; it boots alone to the desktop |
| install onto the MBR disk | erases it (its FAT volume unmounted), installs, boots alone to the desktop with every application in Deskbar's menu; Summit starts and shows its start page |
| live session with the ISO read-only | boots read-only beneath the overlay (`Boot partition is on a hybrid ISO image, using it read-only.`), no panic |
| ISO as a USB CD-ROM | boots read-only beneath the overlay; QEMU's emulated USB CD-ROM then failed a transfer while copying (`usb_disk: sending or receiving of the data failed`), so the NanoKVM's disk mode is used |

## On the ROCK 5 ITX

The ISO is on the NanoKVM as `/data/airos-arm64.iso`, attached **read-only in
disk mode** (`ro=1`, `cdrom=0`; `tools/airos/nanokvm_attach_iso.py`). The board
booted it from EDK2's Boot Manager (*UEFI sipeed NanoKVM*) into the first
boot prompt, and the Installer listed `SPCC M.2 PCIe SSD - 238.47 GiB
[/dev/disk/nvme/0/raw]`, the eMMC (`MMC disk - 7.28 GiB`) and, disabled, the
NanoKVM drive it runs from.

EDK2's boot order now starts with the device entry *UEFI SPCC M.2 PCIe SSD*,
ahead of the old *Haiku NVMe* entry: that one names the current EFI system
partition by its GUID and stops matching after a whole-disk install, which
would leave the board to start the eMMC's older system instead. The NanoKVM
stays behind both, so with an image attached the board still starts the NVMe
unless the NanoKVM is chosen in the Boot Manager (ESC at power-on, *Boot
Manager*).

Before the board was switched to the ISO, its NVMe install's settings and lab
state (`home/config`, the Summit profile, `home/rock5-lab`, `system/settings`,
`system/non-packaged`; 1.2 GB) were copied to
`/mnt/Haiku/HaikuWork-archive/rock5-nvme-backup-20261002`.
