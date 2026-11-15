# VitruvianOS container build

`docker/build.sh` runs the whole build sequentially inside the image from
`docker/Dockerfile`: host buildtools, `configure` (build tree + Debian
chroot), then `bake` (ninja + debs + images). The source tree is bind-mounted
at `/vitruvian`; artifacts land in `generated.<arch>/output/`.

## Usage

```sh
./podman-compose up --build                     # ISO (default)
VOS_IMAGE_TYPES=iso,raw ./podman-compose up --build
VOS_IMAGE_TYPES=none ./podman-compose up --build      # packages only
VOS_BUILD_TYPE=Release ./podman-compose up --build
```

Re-run without rebuilding the image, or run a single step:

```sh
./podman-compose run --rm build ./docker/build.sh
./podman-compose run --rm build ./docker/build.sh configure
./podman-compose run --rm -w /vitruvian/generated.amd64 build ninja
./podman-compose run --rm -w /vitruvian/generated.amd64 build \
    ../bake build --image-type=raw
./podman-compose down
```

Do not run `configure`, `ninja` and `bake` as separate compose services that
`up` starts together: compose starts services in parallel, so ninja runs
before configure has written `build.ninja`.

## Rootless podman

Rootless podman works for the amd64 `iso` and `raw` images: they are assembled
without loop devices (`mke2fs -d`, `mtools`, `xorriso`). The container gets a
privileged user namespace, so `mount`, `chroot` and `unshare` work for
debootstrap.

Board images (`raspberry`, `rockchip`, `allwinner`, `beagle`, `nxp`,
`amlogic`, `visionfive2`, `licheerv`) call `losetup`, which rootless podman
cannot do. Use rootful podman (`sudo ./podman-compose ...`) or build them on
the host.

## Why this layout

- One service, one process: `up` returns when the build finishes.
- `init: true` reaps debootstrap/apt orphans; `privileged: true` is needed for
  the chroot mounts.
- `docker/build.sh` reuses a chroot left at the repo root by older in-tree
  builds, and creates `generated.<arch>/image_tree/chroot` otherwise.
- The image context is trimmed by `.dockerignore`; the Dockerfile only
  installs packages, so `--build` stays fast.
