# Dockerfile for building VitruvianOS (V\OS) images
# Uses Debian trixie (the project's proven build suite)
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

# Containers have no init system; keep service postinsts from starting daemons.
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d \
    && chmod +x /usr/sbin/policy-rc.d

# Host-side tools for the container pipeline:
#   - compiler/CMake/ninja and the resource toolchain buildtools
#   - debootstrap + chroot tooling for the per-arch sysroot
#   - image assembly: mke2fs/mtools/squashfs/xorriso/grub/OVMF (no loop device)
#   - packaging: cpack + dpkg-deb/fakeroot
#   - Wayland server support (libwayland-dev enables VOS_WAYLAND in CMake)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build gcc g++ git curl wget ca-certificates \
    bison flex libfl-dev zlib1g-dev pkg-config file rsync \
    debootstrap debhelper dh-dkms dpkg-dev dkms fakeroot elfutils \
    grub-common grub-efi-amd64-bin grub-pc-bin ovmf \
    xorriso mtools dosfstools e2fsprogs squashfs-tools \
    fdisk parted zstd attr python3 kmod cpio iproute2 udev \
    qemu-user-static qemu-utils sudo proot ccache \
    libwayland-dev libfreetype6-dev libsane-dev libcups2-dev libcairo2-dev libpng-dev libjpeg-dev libtiff-dev libwebp-dev libavif-dev libheif-dev liblcms2-dev libjpeg-dev libicu-dev libglib2.0-dev libudev-dev libblkid-dev libfdisk-dev libinput-dev libevdev-dev libseat-dev libdrm-dev libgbm-dev libxkbcommon-dev libdbus-1-dev libxkbcommon-x11-dev libglib2.0-dev \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /etc/sudoers.d/* \
    && echo 'root ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/vitruvian-root \
    && echo '%sudo ALL=(ALL) NOPASSWD: ALL' >> /etc/sudoers.d/vitruvian-root \
    && chmod 0440 /etc/sudoers.d/vitruvian-root

ARG USERNAME=vitruvian
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && usermod -aG sudo $USERNAME

# The Installer's simulated install (src/apps/installer/tests/sim) formats
# xfs and btrfs roots and checks the BIOS path's grub-install refusal.
RUN apt-get update && apt-get install -y --no-install-recommends \
    xfsprogs btrfs-progs grub2-common \
    && rm -rf /var/lib/apt/lists/*

# Host builds outside the Debian sysroot: libssl-dev for libbe (linked with
# OpenSSL once libshared exists), libpam0g-dev for
# vos-polkit-grant-helper, libbacktrace-dev for Debug builds of libroot.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpam0g-dev libbacktrace-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# media2 (PipeWire, GStreamer, fluidsynth) and the OpenGL kit (Mesa EGL/GL)
# compile against these when the build runs outside the Debian sysroot.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpipewire-0.3-dev libspa-0.2-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev libfluidsynth-dev libegl-dev libgl-dev \
    && rm -rf /var/lib/apt/lists/*

# Device, network, Bluetooth, web and package kits (M9 W2-D): libusb, libnm,
# BlueZ, libre/libvpx (telephony), curl, apt-pkg, libsolv, argon2 (kdbx),
# cairo.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libusb-1.0-0-dev libnm-dev libbluetooth-dev libre-dev libvpx-dev \
    libcurl4-gnutls-dev libapt-pkg-dev libsolv-dev libsolvext-dev \
    libargon2-dev libcairo2-dev \
    && rm -rf /var/lib/apt/lists/*

# MediaConverter (FFmpeg) and Cortex (expat), M9 W4 app ports.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libavcodec-dev libavformat-dev libavfilter-dev libswscale-dev \
    libswresample-dev libavutil-dev libexpat1-dev \
    && rm -rf /var/lib/apt/lists/*

# Mesa hgl (src/add-ons/opengl): meson and Mako build Mesa's Haiku platform
# with the Zink renderer, which compiles against the Vulkan headers and
# libzstd; the air/OS and Koan series are applied with patch.
RUN apt-get update && apt-get install -y --no-install-recommends \
    meson python3-mako libvulkan-dev libzstd-dev patch \
    && rm -rf /var/lib/apt/lists/*

# Translator add-ons (src/add-ons/translators): the image codecs not
# already installed above (PNG, JPEG, TIFF, WebP and AVIF are).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libraw-dev libicns-dev libgif-dev libva-dev libdvdnav-dev \
    && rm -rf /var/lib/apt/lists/*

# Gutenprint printer driver add-on (src/add-ons/print/drivers/gutenprint).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgutenprint-dev \
    && rm -rf /var/lib/apt/lists/*

# index_server add-ons (TagLib, CLucene) and Quick Look's PDF previews (MuPDF).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libtag-dev libclucene-dev libmupdf-dev \
    && rm -rf /var/lib/apt/lists/*

ENV DEBIAN_MIRROR=http://deb.debian.org/debian/
ENV VOS_BASE_SUITE=trixie
ENV VOS_KERNEL_FLAVOUR=rt-amd64
ENV VOS_ARCH=amd64
ENV VOS_BUILD_TYPE=Debug
ENV VOS_IMAGE_TYPES=iso

WORKDIR /vitruvian

# The pipeline runs configure + bake sequentially; see docker/build.sh.
CMD ["/vitruvian/docker/build.sh"]
