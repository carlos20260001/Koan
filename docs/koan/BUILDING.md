# Building this tree with the container (CMake/Debian pipeline)

This tree carries two builds side by side (see `PORTING.md`):

- **Jam** (air/OS/Haiku): `./configure` + `jam`. Being retired: the Haiku
  kernel is gone and directories drop their Jamfile once CMake builds them,
  so Jam no longer produces a bootable image.
- **CMake/Debian** (VitruvianOS, ported subset): `vos-configure` + `ninja` +
  `bake`, run inside Koan's Podman container by `docker/build.sh`.

This page covers the second one. `docker/README.md` is Koan's own note on the
container; it applies here except where this page says otherwise.

## Requirements (host)

- rootless `podman` and `podman-compose` (or the `podman compose` plugin)
- ~8 GiB free for a Release tree, ~20 GiB for Debug (`df -h /`)
- network access for the first Debian chroot (debootstrap)

Everything else (cmake, ninja, bison/flex, debootstrap, dkms, debhelper,
xorriso, ...) is in the image from `docker/Dockerfile`.

## Commands

```sh
./podman-compose build                                  # the image only
VOS_IMAGE_TYPES=none VOS_BUILD_TYPE=Release ./podman-compose up --build
./podman-compose down
```

The service's container is called `vitruvian-airos-build`, not Koan's
`vitruvian-build`: `podman-compose up` starts an existing container of the
same name instead of creating one, and Koan's container bind-mounts the Koan
checkout.

Steps run by `docker/build.sh`, in order:

1. host buildtools (`rc`, `xres`, `resattr`) in `buildtools/`
2. `vos-configure --arch=... --build-type=... --chroot-build --buildtools=...`
   in the build tree, which also creates the Debian chroot
   (`<build dir>/image_tree/chroot`) with debootstrap
3. `ninja` (with `VOS_IMAGE_TYPES=none`) or `bake build --image-type=...`

`vos-configure` is VitruvianOS's `configure`, renamed because the root
`configure` is Haiku's. Run it from inside a build directory as
`../vos-configure`, or from anywhere with `VOS_CMAKE_SOURCE_DIR=<source dir>`.
Always call `vos-configure` directly; Haiku's `configure` does not know its
options.

## Environment

Passed through `docker-compose.yml`; see the header of `docker/build.sh`.

| Variable | Default | Meaning |
| --- | --- | --- |
| `VOS_ARCH` | `amd64` | target architecture |
| `VOS_BUILD_TYPE` | `Debug` | `Debug` or `Release` |
| `VOS_IMAGE_TYPES` | `iso` | `none`, or a comma list of `iso`, `raw`, board types (boards need rootful podman) |
| `VOS_BUILD_DIR` | `generated.<arch>` | build tree on disk; also where `output/` is synced |
| `VOS_TMPFS_BUILD` | `auto` | build on a tmpfs inside the container when `/tmp` has room; the tree is discarded afterwards, only `output/` is kept. `0` builds on disk |
| `VOS_CCACHE` | `auto` | ccache via `CMAKE_<LANG>_COMPILER_LAUNCHER` |
| `VOS_CMAKE_EXTRA_OPTS` | empty | extra `-D` options for CMake |
| `VOS_SSHDEBUG` | `0` | stage the SSH debug boot entry |
| `VOS_BUILDTOOLS_DIR` | empty | prebuilt buildtools tree, see below |
| `VOS_WITH_WLROOTS` | empty | inert here, see below |
| `DEBIAN_MIRROR`, `VOS_BASE_SUITE`, `VOS_KERNEL_FLAVOUR` | Debian, `trixie`, `rt-amd64` | chroot/image base |

## Current state

Observed on 2026-11-15 (amd64, rootless podman, Release). Logs are not
kept in git.

- `./podman-compose build`: the image builds (all layers came from the cache
  of an earlier Koan build; the Dockerfile is Koan's, unchanged).
- `VOS_IMAGE_TYPES=none VOS_BUILD_TYPE=Release ./podman-compose up --build`:
  **stops at step 1** with the buildtools message below, exit 1. The tree
  cannot build its own buildtools until `src/system/libroot2` (M4) and the
  kits land.
- With prebuilt buildtools and a repo key (both escape hatches below):
  debootstrap creates the trixie chroot (with the Debian rt kernel,
  6.12.117+deb13-rt-amd64), `vos-configure` configures and generates,
  `ninja` runs its two default steps (app attrs, Tracker template attrs),
  exit 0. Nothing is compiled: the only real target in the ported subset is
  `nexus` (the nexus-dkms deb), which the default `ninja` does not build;
  run `ninja nexus` in the build tree for it (milestone M3).
- `generated.<arch>/output/` stays empty: no image, no `vos` deb content yet.

Known gaps:

- **Repo key.** The chroot setup (`build/scripts/lib/chroot.sh`) adds the
  V\OS apt source and needs its public key at
  `data/keys/vitruvian-archive-keyring.asc`, which is not in this tree yet;
  without it the run dies right after debootstrap with
  `V\OS repo key not found`. Point `VOS_REPO_KEY` at a copy of that file
  inside the source tree (for example
  `git show upstream/master:data/keys/vitruvian-archive-keyring.asc`
  saved under `generated.keys/`), or import the file. A failed run leaves
  `image_tree/chroot` behind, and the next run then skips the chroot
  setup; delete it first (`podman unshare rm -rf generated.<arch>/image_tree`).
  In the observed run apt could not reach `https://repo.v-os.dev`
  (certificate verify failed); it only warned and went on with Debian.
- **Git worktrees.** In a `git worktree` checkout `.git` is a file pointing
  outside the bind mount, so git does not work in the container: CMake warns
  that the package version has no git sha and the build is not
  byte-reproducible, and nexus-dkms gets version `0.3.0+git0.unknown`.
  Build from a regular clone for release packages.
- Files the container creates as root (chroot, apt cache) belong to
  sub-uids on the host; remove them with `podman unshare rm -rf ...`.

## Escape hatch: prebuilt buildtools

The buildtools pass (`cmake -DBUILDTOOLS_MODE=1`) compiles `src/build/libroot`
and `src/build/libbe`, whose sources live in `src/system/libroot2` (PORTING.md
M4) and the VitruvianOS kits. Until they land, `docker/build.sh` stops with a
message saying so. To get past it, place a buildtools tree built elsewhere
(a VitruvianOS or Koan checkout's `buildtools/`, or the contents of the
`vos-buildtools` deb) **inside the source tree**, since only the source tree is
mounted in the container:

```sh
mkdir -p prebuilt-buildtools/src/bin/rc prebuilt-buildtools/src/tools/locale
cp <other>/buildtools/src/bin/rc/rc         prebuilt-buildtools/src/bin/rc/
cp <other>/buildtools/src/bin/{xres,resattr} prebuilt-buildtools/src/bin/
cp <other>/buildtools/src/tools/locale/linkcatkeys prebuilt-buildtools/src/tools/locale/
VOS_BUILDTOOLS_DIR=prebuilt-buildtools VOS_IMAGE_TYPES=none \
    VOS_BUILD_TYPE=Release ./podman-compose up --build
```

The tools must run in the container (built against Debian trixie's glibc).
`prebuilt-buildtools/` and `buildtools/` are git-ignored and excluded from
the image context. A tree copied to `buildtools/` is also reused when its tools
run.

## Wayland server

`VOS_WITH_WLROOTS` and `--with-wlroots` are kept from Koan but do nothing
here: `src/add-ons/wlroots-koan` is not in this tree. `docker/build.sh`
notes that it ignores the variable and does not pass it to CMake.
