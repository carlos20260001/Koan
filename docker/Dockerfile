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

ENV DEBIAN_MIRROR=http://deb.debian.org/debian/
ENV VOS_BASE_SUITE=trixie
ENV VOS_KERNEL_FLAVOUR=rt-amd64
ENV VOS_ARCH=amd64
ENV VOS_BUILD_TYPE=Debug
ENV VOS_IMAGE_TYPES=iso

WORKDIR /vitruvian

# The pipeline runs configure + bake sequentially; see docker/build.sh.
CMD ["/vitruvian/docker/build.sh"]
