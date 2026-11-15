#!/bin/sh

qemu_inject() {
    _chroot_dir="$1"
    _target_arch="$2"
    if ! is_cross_build "$_target_arch"; then
        return 0
    fi
    _qemu_bin="$(find_qemu_user_binary "$_target_arch")"
    _qemu_name="$(basename "$_qemu_bin")"
    log_info "Cross-build detected: injecting $_qemu_name into chroot"
    vrun cp "$_qemu_bin" "$_chroot_dir/usr/bin/$_qemu_name"
    vrun chmod +x "$_chroot_dir/usr/bin/$_qemu_name"
    register_binfmt
}

qemu_eject() {
    _chroot_dir="$1"
    _target_arch="$2"
    if ! is_cross_build "$_target_arch"; then
        return 0
    fi
    _qemu_name="$(arch_to_qemu_user "$_target_arch")"
    vrun rm -f "$_chroot_dir/usr/bin/$_qemu_name" \
        "$_chroot_dir/usr/bin/${_qemu_name%-static}" 2>/dev/null || true
}

# Debian mirror for debootstrap/apt in the chroot; override with DEBIAN_MIRROR.
: "${DEBIAN_MIRROR:=http://deb.debian.org/debian/}"

# Debian suite for the chroot and board rootfs; only trixie is proven.
: "${VOS_BASE_SUITE:=trixie}"

# The V\OS apt source. The public key ships in the tree (and in
# vos-archive). The suite pairs the Debian base with the channel:
# VOS_CHANNEL=release gives trixie/testing, nightly (the default)
# trixie-nightly/testing-nightly. VOS_REPO_SUITE overrides both.
vos_repo_source()
{
    _root="$1"
    : "${VOS_REPO_URL:=https://repo.v-os.dev}"
    : "${VOS_REPO_KEY:=$LIB_DIR/../../../data/keys/vitruvian-archive-keyring.asc}"
    case "${VOS_CHANNEL:=nightly}" in
        release) : "${VOS_REPO_SUITE:=$VOS_BASE_SUITE}" ;;
        nightly) : "${VOS_REPO_SUITE:=$VOS_BASE_SUITE-nightly}" ;;
        *) die "VOS_CHANNEL='$VOS_CHANNEL' is not release or nightly" ;;
    esac
    [ -f "$VOS_REPO_KEY" ] || die "V\\OS repo key not found: $VOS_REPO_KEY"
    sudo install -d -m 755 "$_root/usr/share/keyrings" \
        "$_root/etc/apt/sources.list.d"
    sudo install -m 644 "$VOS_REPO_KEY" \
        "$_root/usr/share/keyrings/vitruvian-archive-keyring.asc"
    sudo tee "$_root/etc/apt/sources.list.d/vitruvian.sources" >/dev/null <<VOSSRC
Types: deb
URIs: $VOS_REPO_URL
Suites: $VOS_REPO_SUITE
Components: main
Signed-By: /usr/share/keyrings/vitruvian-archive-keyring.asc
VOSSRC
    log_info "V\\OS repo enabled: $VOS_REPO_URL $VOS_REPO_SUITE"

    # Koan builds its own vos, vos-*, dh-vos, netplus-extensions and
    # nexus-dkms. Never take those from the V\OS repo: an upstream vos
    # would replace Koan's runtime (and its desktop) on the next upgrade.
    # Debian and other packages from the repo stay installable.
    : "${VOS_REPO_PIN_PACKAGES:=vos vos-* dh-vos netplus-extensions nexus-dkms}"
    _repo_host="${VOS_REPO_URL#*://}"
    _repo_host="${_repo_host%%/*}"
    _repo_host="${_repo_host%%:*}"
    sudo install -d -m 755 "$_root/etc/apt/preferences.d"
    sudo tee "$_root/etc/apt/preferences.d/koan-local-packages.pref" >/dev/null <<VOSPIN
Explanation: Koan-built packages must not come from the V\\OS repo.
Package: $VOS_REPO_PIN_PACKAGES
Pin: origin "$_repo_host"
Pin-Priority: -1
VOSPIN
    log_info "Pinned out of $_repo_host: $VOS_REPO_PIN_PACKAGES"
}

# Persistent per-arch .deb cache shared across chroot regenerations.
chroot_cache_dir() {
    echo "$1/deb"
}

chroot_mount() {
    _chroot_dir="$1"
    [ -d "$_chroot_dir" ] || die "Chroot directory not found: $_chroot_dir"

    vrun mkdir -p "$_chroot_dir/proc" "$_chroot_dir/sys" "$_chroot_dir/dev"

    if [ "$VOS_ROOTLESS" -eq 1 ]; then
        # Rootless: cannot mount proc/sys/dev. Just ensure directories exist
        # and copy resolv.conf; apt will run via dpkg --root on the host instead.
        vrun cp -L /etc/resolv.conf "$_chroot_dir/etc/resolv.conf" 2>/dev/null || true
        return 0
    fi

    if ! mountpoint -q "$_chroot_dir/proc" 2>/dev/null; then
        vrun mount -t proc / "$_chroot_dir/proc"
    fi
    if ! mountpoint -q "$_chroot_dir/sys" 2>/dev/null; then
        vrun mount --rbind /sys "$_chroot_dir/sys"
        vrun mount --make-rslave "$_chroot_dir/sys"
    fi
    if ! mountpoint -q "$_chroot_dir/dev" 2>/dev/null; then
        vrun mount --rbind /dev "$_chroot_dir/dev"
        vrun mount --make-rslave "$_chroot_dir/dev"
    fi

    # Provision DNS so `apt update` inside the chroot can reach the mirrors.
    # -L dereferences the host's systemd-resolved stub symlink.
    vrun mkdir -p "$_chroot_dir/etc"
    vrun cp -L /etc/resolv.conf "$_chroot_dir/etc/resolv.conf"
}

# chroot_isolated ROOT CMD [ARGS...]
# Runs CMD chrooted into ROOT with its own network, hostname, IPC, PID and mount namespaces and a
# read-only /proc/sys and /sys, so maintainer scripts cannot touch the host. /dev stays the host's.
_CHROOT_ISOLATED_INNER='r="$1"; shift
ip link set lo up 2>/dev/null
mount -t proc proc "$r/proc" || exit 1
mount --bind "$r/proc/sys" "$r/proc/sys" || exit 1
mount -o remount,bind,ro "$r/proc/sys" || exit 1
if mountpoint -q "$r/sys"; then umount -R "$r/sys" 2>/dev/null || true; fi
mount -t sysfs -o ro sysfs "$r/sys" || exit 1
exec chroot "$r" "$@"'

chroot_isolated() {
    _ci_root="$1"
    shift
    vrun mkdir -p "$_ci_root/proc" "$_ci_root/sys"

    if [ "$VOS_ROOTLESS" -eq 1 ]; then
        # Rootless: cannot use unshare namespaces. Run via proot if available,
        # otherwise directly (some operations may fail without root).
        if command -v proot >/dev/null 2>&1; then
            proot -r "$_ci_root" "$@"
        else
            # Fallback: run directly in the directory (not truly chrooted).
            # apt/dpkg operations use --root instead, so this is mainly for
            # non-apt commands like debootstrap second stage.
            ( cd "$_ci_root" && "$@" )
        fi
        return
    fi

    vrun unshare --net --uts --ipc --pid --fork --mount --propagation private \
        /bin/sh -c "$_CHROOT_ISOLATED_INNER" sh "$_ci_root" "$@"
}

chroot_umount() {
    _chroot_dir="$1"
    [ -d "$_chroot_dir" ] || return 0

    if [ "$VOS_ROOTLESS" -eq 1 ]; then
        # Rootless: nothing was mounted, nothing to umount.
        return 0
    fi

    vrun umount -l "$_chroot_dir/var/cache/apt/archives" 2>/dev/null || true
    vrun umount -l "$_chroot_dir/proc" 2>/dev/null || true
    vrun umount -l "$_chroot_dir/sys" 2>/dev/null || true
    #vrun umount -l "$_chroot_dir/dev/pts" 2>/dev/null || true
    vrun umount -l "$_chroot_dir/dev" 2>/dev/null || true
}

# create_iso purges the -dev packages from the chroot it squashes; put
# back whatever is missing before compiling. The .deb cache makes it an unpack.
chroot_restore_dev_packages() {
    _crd_basedir="$1"
    _crd_arch="$2"
    _crd_dir="$_crd_basedir/image_tree/chroot"
    [ -d "$_crd_dir" ] || return 0

    if [ "$VOS_ROOTLESS" -eq 1 ]; then
        # Rootless: cannot run apt-get inside chroot without proot.
        # Packages are installed during chroot creation via dpkg --root.
        log_info "Rootless: skipping dev package restore (installed during chroot creation)"
        return 0
    fi

    # Installed names plus what they provide (libfreetype6-dev is only provided).
    _crd_have="$(dpkg-query --admindir="$_crd_dir/var/lib/dpkg" -W \
        -f='${db:Status-Abbrev} ${Package},${Provides}\n' 2>/dev/null \
        | awk '$1 == "ii" { sub(/^ii +/, ""); n = split($0, a, ","); \
            for (i = 1; i <= n; i++) { sub(/^ +/, "", a[i]); sub(/ .*/, "", a[i]); \
            if (a[i] != "") print a[i] } }')"
    _crd_missing=""
    for _p in $(get_dev_packages "$_crd_arch"); do
        printf '%s\n' "$_crd_have" | grep -qx "$_p" || _crd_missing="$_crd_missing $_p"
    done
    [ -n "$_crd_missing" ] || return 0
    log_step "Reinstalling build packages:$_crd_missing"
    qemu_inject "$_crd_dir" "$_crd_arch"
    chroot_mount "$_crd_dir"
    chroot_mount_deb_cache "$_crd_dir" "$(chroot_cache_dir "$_crd_basedir")"
    vchroot "$_crd_dir" /usr/bin/env $CHROOT_PROXY DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 \
        apt-get install -y --download-only --no-install-recommends $_crd_missing \
        || die "build package download failed"
    chroot_isolated "$_crd_dir" /usr/bin/env $CHROOT_PROXY DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 \
        apt-get install -y --no-install-recommends $_crd_missing \
        || die "build package reinstall failed"
    chroot_umount "$_crd_dir"
    qemu_eject "$_crd_dir" "$_crd_arch"
}

# Bind the persistent .deb cache into the chroot. Idempotent.
chroot_mount_deb_cache() {
    _chroot_dir="$1"
    _cache_dir="$2"
    vrun mkdir -p "$_cache_dir/archives/partial"
    vrun mkdir -p "$_chroot_dir/var/cache/apt/archives"
    if [ "$VOS_ROOTLESS" -eq 1 ]; then
        # Rootless: cannot bind-mount. Copy debs instead.
        vrun cp -n "$_cache_dir"/archives/*.deb "$_chroot_dir/var/cache/apt/archives/" 2>/dev/null || true
        return 0
    fi
    if ! mountpoint -q "$_chroot_dir/var/cache/apt/archives" 2>/dev/null; then
        vrun mount --bind "$_cache_dir/archives" \
            "$_chroot_dir/var/cache/apt/archives"
    fi
    # Keep downloaded .debs (default drops them after install), via a drop-in config.
    vrun mkdir -p "$_chroot_dir/etc/apt/apt.conf.d"
    echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' \
        | vrun tee "$_chroot_dir/etc/apt/apt.conf.d/99-keep-debs" >/dev/null
    cat <<'EOF' | vrun tee "$_chroot_dir/etc/apt/apt.conf.d/90-vos-gzip-indexes" >/dev/null
Acquire::GzipIndexes "true";
Acquire::CompressionTypes::Order { "gz"; };
EOF
}

chroot_create() {
    _basedir="$1"
    _arch="$2"
    _deb_arch="$(arch_to_deb "$_arch")"
    _chroot_dir="$_basedir/image_tree/chroot"

    if [ -d "$_chroot_dir" ]; then
        die "A chroot already exists at $_chroot_dir. Run bake.sh --regenerate-chroot to recreate it."
    fi

    mkdir -p "$_basedir/image_tree"

    _cache_dir="$(chroot_cache_dir "$_basedir")"
    _debootstrap_cache="$_cache_dir/debootstrap"
    vrun mkdir -p "$_debootstrap_cache" "$_cache_dir/archives/partial"
    log_info "Using package cache: $_cache_dir (mirror: $DEBIAN_MIRROR)"

    if [ "$VOS_ROOTLESS" -eq 1 ]; then
        chroot_create_rootless "$_basedir" "$_arch" "$_deb_arch" "$_chroot_dir" "$_debootstrap_cache" "$_cache_dir"
        return
    fi

    if is_cross_build "$_arch"; then
        log_step "Bootstrapping Debian $VOS_BASE_SUITE ($_deb_arch) [foreign]..."
        vrun debootstrap --arch="$_deb_arch" --variant=minbase --foreign \
            --cache-dir="$_debootstrap_cache" \
            "$VOS_BASE_SUITE" "$_chroot_dir" "$DEBIAN_MIRROR"
        qemu_inject "$_chroot_dir" "$_arch"
    else
        log_step "Bootstrapping Debian $VOS_BASE_SUITE ($_deb_arch)..."
        vrun debootstrap --arch="$_deb_arch" --variant=minbase \
            --cache-dir="$_debootstrap_cache" \
            "$VOS_BASE_SUITE" "$_chroot_dir" "$DEBIAN_MIRROR"
    fi

    trap 'chroot_umount "$_chroot_dir"' EXIT
    chroot_mount "$_chroot_dir"
    chroot_mount_deb_cache "$_chroot_dir" "$_cache_dir"

    # Pin apt to the debootstrap mirror.
    : "${DEBIAN_SECURITY_MIRROR:=http://security.debian.org/debian-security}"
    vrun tee "$_chroot_dir/etc/apt/sources.list" >/dev/null <<EOF
deb $DEBIAN_MIRROR $VOS_BASE_SUITE main contrib non-free non-free-firmware
deb $DEBIAN_MIRROR $VOS_BASE_SUITE-updates main contrib non-free non-free-firmware
deb $DEBIAN_SECURITY_MIRROR $VOS_BASE_SUITE-security main contrib non-free non-free-firmware
EOF

    vos_repo_source "$_chroot_dir"

    log_step "Verifying mount points before second-stage..."
    log_info "Checking proc: mountpoint=$(mountpoint -q "$_chroot_dir/proc" 2>/dev/null && echo yes || echo no), stat=$([ -f "$_chroot_dir/proc/1/stat" ] && echo exists || echo missing)"
    if mountpoint -q "$_chroot_dir/proc" 2>/dev/null; then
        log_info "proc mounted"
    elif [ -f "$_chroot_dir/proc/1/stat" ]; then
        log_info "proc accessible"
    else
        die "proc mount failed - cannot proceed with second-stage"
    fi
    mountpoint -q "$_chroot_dir/sys" || die "sys mount failed"
    mountpoint -q "$_chroot_dir/dev" || die "dev mount failed"

    if is_cross_build "$_arch"; then
        # Second stage unpacks from /var/cache/apt/archives, bound to $_cache_dir/archives.
        vrun sh -c 'cp -n "$1"/*.deb "$2"/ 2>/dev/null || true' _ \
            "$_debootstrap_cache" "$_cache_dir/archives"
        log_step "Running debootstrap second stage..."
        chroot_isolated "$_chroot_dir" /debootstrap/debootstrap --second-stage
        log_step "Re-mounting after second-stage..."
        chroot_mount "$_chroot_dir"
    fi

    log_step "Verifying mount points after second-stage..."
    if mountpoint -q "$_chroot_dir/proc" 2>/dev/null; then
        log_info "proc mounted"
    elif [ -f "$_chroot_dir/proc/1/stat" ]; then
        log_info "proc accessible"
    else
        log_warn "proc mount may have failed - continuing anyway"
    fi

    log_step "Creating essential directories..."
    vrun mkdir -p "$_chroot_dir/dev"
    vrun mkdir -p "$_chroot_dir/proc"
    vrun mkdir -p "$_chroot_dir/sys"
    vrun mkdir -p "$_chroot_dir/run"
    vrun mkdir -p "$_chroot_dir/tmp"
    for _d in dev/hugepages dev/mqueue run/lock \
              sys/kernel/debug sys/kernel/tracing \
              sys/fs/fuse/connections sys/kernel/config; do
        vrun mkdir -p "$_chroot_dir/$_d"
    done

    _base_pkgs="$(get_base_packages "$_arch")"
    _dev_pkgs="$(get_dev_packages "$_arch")"

    log_step "Installing packages..."
    # Skip dpkg fsyncs on a throwaway chroot; opt-in.
    if [ "${VOS_UNSAFE_IO:-0}" = 1 ]; then
        vrun install -d -m 755 "$_chroot_dir/etc/dpkg/dpkg.cfg.d"
        printf 'force-unsafe-io\n' \
          | vrun tee "$_chroot_dir/etc/dpkg/dpkg.cfg.d/vos-build-unsafe-io" >/dev/null
    fi
    # Download with the network, install isolated: cups-pdf's postinst ran lpadmin against the host's cupsd.
    vrun chroot "$_chroot_dir" /usr/bin/env $CHROOT_PROXY DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 /bin/bash -c "\
echo 'vitruvian' > /etc/hostname && \
apt update && apt install -y --download-only --no-install-recommends $_base_pkgs $_dev_pkgs \$DEBUG_PACKAGES"
    if [ "${VOS_NATIVE_UNPACK:-0}" = 1 ]; then
        # Unpack with the host dpkg (no emulation); configure stays in-chroot.
        # Any refusal falls back to the emulated apt transaction.
        _native_ok=0
        if native_pkg_unpack "$_chroot_dir" \
           && chroot_isolated "$_chroot_dir" /usr/bin/env $CHROOT_PROXY DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 /bin/bash -c "\
dpkg --configure -a && \
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen && locale-gen && \
exit"; then
            _native_ok=1
        else
            log_warn "native unpack path failed — falling back to emulated apt install"
        fi
    fi
    if [ "${_native_ok:-0}" = 0 ]; then
        chroot_isolated "$_chroot_dir" /usr/bin/env $CHROOT_PROXY DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 /bin/bash -c "\
apt install -y --no-install-recommends $_base_pkgs $_dev_pkgs \$DEBUG_PACKAGES && \
echo 'en_US.UTF-8 UTF-8' > /etc/locale.gen && locale-gen && \
exit"
    fi

    ls "$_chroot_dir/lib/modules" | head -n1 > "$_basedir/imagekernelversion.conf"

    qemu_eject "$_chroot_dir" "$_arch"
    chroot_umount "$_chroot_dir"

    log_info "Chroot created at $_chroot_dir"
}

# chroot_create_rootless — bootstrap a Debian chroot without mount/chroot/sudo.
# Uses debootstrap --foreign --no-check-gpg (best effort; device nodes may be
# missing) and proot (if available) or dpkg --root for package management.
chroot_create_rootless() {
    _basedir="$1"
    _arch="$2"
    _deb_arch="$3"
    _chroot_dir="$4"
    _debootstrap_cache="$5"
    _cache_dir="$6"

    log_step "Bootstrapping Debian $VOS_BASE_SUITE ($_deb_arch) [rootless]..."

    # debootstrap --foreign runs first stage (download + extract) without
    # configuring. --no-check-gpg skips signature verification. As non-root
    # it may fail to create device nodes; we proceed anyway.
    vrun debootstrap --arch="$_deb_arch" --variant=minbase --foreign --no-check-gpg \
        --cache-dir="$_debootstrap_cache" \
        "$VOS_BASE_SUITE" "$_chroot_dir" "$DEBIAN_MIRROR" || \
        log_warn "debootstrap first stage had errors (device nodes may be missing)"

    qemu_inject "$_chroot_dir" "$_arch"

    # Write apt sources (no mount needed).
    : "${DEBIAN_SECURITY_MIRROR:=http://security.debian.org/debian-security}"
    cat > "$_chroot_dir/etc/apt/sources.list" <<EOF
deb $DEBIAN_MIRROR $VOS_BASE_SUITE main contrib non-free non-free-firmware
deb $DEBIAN_MIRROR $VOS_BASE_SUITE-updates main contrib non-free non-free-firmware
deb $DEBIAN_SECURITY_MIRROR $VOS_BASE_SUITE-security main contrib non-free non-free-firmware
EOF

    # VitruvianOS repo (if key supplied).
    : "${VOS_REPO_URL:=https://repo.v-os.dev}"
    if [ -n "${VOS_REPO_KEY:-}" ] && [ -f "$VOS_REPO_KEY" ]; then
        [ -n "${VOS_REPO_SUITE:-}" ] || die "VOS_REPO_SUITE is unset and a repo key was supplied"
        mkdir -p "$_chroot_dir/etc/apt/keyrings"
        cp "$VOS_REPO_KEY" "$_chroot_dir/etc/apt/keyrings/vitruvian-archive-keyring.asc"
        mkdir -p "$_chroot_dir/etc/apt/sources.list.d"
        cat > "$_chroot_dir/etc/apt/sources.list.d/vitruvian.sources" <<VOSEOF
Types: deb
URIs: $VOS_REPO_URL
Suites: $VOS_REPO_SUITE
Components: main
Signed-By: /etc/apt/keyrings/vitruvian-archive-keyring.asc
VOSEOF
        log_info "VitruvianOS repo enabled: $VOS_REPO_URL $VOS_REPO_SUITE"
    else
        log_warn "VOS_REPO_KEY unset or missing; image will NOT see the VitruvianOS repo"
    fi

    # Copy deb cache into chroot (no bind mount possible).
    chroot_mount_deb_cache "$_chroot_dir" "$_cache_dir"

    log_step "Creating essential directories..."
    mkdir -p "$_chroot_dir/dev" "$_chroot_dir/proc" "$_chroot_dir/sys" \
        "$_chroot_dir/run" "$_chroot_dir/tmp"
    for _d in dev/hugepages dev/mqueue run/lock \
              sys/kernel/debug sys/kernel/tracing \
              sys/fs/fuse/connections sys/kernel/config; do
        mkdir -p "$_chroot_dir/$_d"
    done

    _base_pkgs="$(get_base_packages "$_arch")"
    _dev_pkgs="$(get_dev_packages "$_arch")"

    log_step "Installing packages via dpkg --root (rootless)..."
    _tmp_debs="$(mktemp -d)"
    cd "$_tmp_debs"

    # Download packages for the target architecture (amd64 host → amd64 target works).
    for _p in $_base_pkgs $_dev_pkgs; do
        apt-get download "$_p" 2>/dev/null || true
    done

    # Install each deb into chroot directory (--force-not-root for non-root).
    _dpkg_flags="--root=$_chroot_dir --force-not-root"
    [ "$VOS_ROOTLESS" -ne 1 ] || _dpkg_flags="$_dpkg_flags --force-not-root"

    for _deb_file in *.deb; do
        [ -f "$_deb_file" ] || continue
        dpkg $_dpkg_flags --install "$_deb_file" 2>/dev/null || true
    done
    dpkg $_dpkg_flags --configure -a 2>/dev/null || \
        log_warn "dpkg --configure had errors (expected for some packages rootless)"

    cd "$_basedir"
    rm -rf "$_tmp_debs"

    # Generate locales manually (locale-gen needs chroot)
    echo 'en_US.UTF-8 UTF-8' > "$_chroot_dir/etc/locale.gen"
    if command -v proot >/dev/null 2>&1; then
        proot -r "$_chroot_dir" locale-gen 2>/dev/null || true
    else
        # Without proot, locale-gen can't run inside the chroot.
        # The target system will generate locales on first boot.
        log_warn "locale-gen skipped (no proot available for rootless chroot)"
    fi

    # Set hostname
    echo 'vitruvian' > "$_chroot_dir/etc/hostname"

    ls "$_chroot_dir/lib/modules" | head -n1 > "$_basedir/imagekernelversion.conf" 2>/dev/null || \
        echo "unknown" > "$_basedir/imagekernelversion.conf"

    qemu_eject "$_chroot_dir" "$_arch"

    log_info "Chroot created at $_chroot_dir (rootless)"
}

chroot_regenerate() {
    _basedir="$1"
    _arch="$2"
    _chroot_dir="$_basedir/image_tree/chroot"

    if [ -d "$_chroot_dir" ]; then
        log_step "Regenerating chroot..."
        chroot_umount "$_chroot_dir"
        if [ "$VOS_ROOTLESS" -ne 1 ]; then
            # A bind mount still attached would let rm reach the host.
            if findmnt -rn -o TARGET | grep -qF "$_chroot_dir/"; then
                die "Still mounted under $_chroot_dir; unmount it and retry."
            fi
        fi
        vrun rm -rf --one-file-system "$_chroot_dir"
    fi

    chroot_create "$_basedir" "$_arch"
}
