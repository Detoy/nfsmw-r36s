# Need for Speed: Most Wanted (Android) — R36S port

> **Public alpha:** races are playable on a physical R36S, but one touch-first
> pre-race car-selection/purchase action is not yet controller-accessible after
> its rollout animation settles. Read [KNOWN_ISSUES.md](KNOWN_ISSUES.md) before
> installing.

This directory contains the clean-room compatibility work for running the
user-supplied Android release of *Need for Speed: Most Wanted* on the R36S.
It does not redistribute the game, its APK, its OBB, or FMOD.

## Tested source build

- Package: `com.ea.games.nfs13_row`
- Version: `1.3.128` (`1003128`)
- APK SHA-256: `bfbe9d08165b8e976924e94879b40ac6575108d5b92521ca837175c0b291c7c7`
- OBB SHA-256: `66dd4e695e698929f789e7c825eabe3ba5a50ed2ce28b628c96e5dbc008043a1`
- Native ABI: Android ARMv7, soft-float procedure-call convention, Thumb-2,
  VFPv3 and NEON
- Graphics: OpenGL ES 2.0

`com.ea.nfsmw_row_v1.3.128-1030128_Android-4.4.apk` in the source folder is
not the game. It is a PPSSPP-based repack and must not be used by this port.

## Current milestone

Mapping, all 56,142 relocations, the complete Bionic/soft-float boundary, and
all guest constructors/finalizers have passed on the physical R36S. The
current consolidated build contains the compressed-OBB index, fake JavaVM,
persistent 640x480 GLES2 lifecycle, SDL controller dispatch, smooth analog
steering, standard D-pad menu navigation, repeated game ticks, FPS logging,
and an FMOD AudioTrack-to-SDL mixer pump. Multiple races have completed on the
physical handheld at the high visual tier. Run 27 sustained 48.84 FPS overall
with working sound effects; soundtrack playback remains disabled because its
failed decoder retry loop halves performance. Runs 30–32 narrowed the remaining
controller defect to the title's touch-first `car_select_new` screen and were
reverted; the public alpha ships the stable Run 29 mapper.

## Controls

- Left stick: smooth steering and two-dimensional map movement
- D-pad: native MOGA menu navigation
- A: drift/accept; B: back; L1: brake/reverse; R1: nitrous
- L1/R1: change top-level menu section while in the front end
- Start: pause and map event selection
- Select+Start: exit cleanly to PortMaster

The modifications screen works with D-pad and A. On the preceding car-selection
or purchase screen, A works only during the short rollout window. The proven
workaround is B to leave, A to re-enter, then A again immediately. Select's
experimental cursor is present for debugging but its synthetic taps are
rejected by the game and it is not a working fallback.

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the community help request,
[PORTING_STATUS.md](PORTING_STATUS.md) for all gates and hardware runs, and
[research/abi-contract.md](research/abi-contract.md) for the native contract.

## Install the alpha

1. Download `nfsmw-r36s-v0.1.0-alpha.zip` from the GitHub release.
2. Install it through PortMaster or extract it at the root of the ROMs card.
3. Put the supported APK and OBB in `ports/nfsmw/gamedata/`.
4. Start the port. First launch verifies both files and extracts five ARMv7
   libraries locally.

This alpha is intended for ArkOS on the R36S. Other ARMv7 PortMaster devices
may work, but have not been validated by this project.

## Prepare private game files

```sh
tools/extract_nfsmw.sh \
  NFS-Most-Wanted-v1.3.128-www.ReXdl.com.apk \
  com.ea.games.nfs13_row/main.1003128.com.ea.games.nfs13_row.obb \
  gamefiles
```

The extractor hash-checks both inputs, stages only the ARMv7 libraries and APK
assets, copies the OBB without expanding its roughly 2.1 GB payload, and writes
a versioned readiness marker.

To repeat the native inventory:

```sh
tools/audit_native.py gamefiles/android-libs
```

## Legal boundary

Only compatibility code, scripts, documentation, and hashes belong in a
release. Users must provide their own legally obtained APK and OBB. The build
and release process must never package the extracted `.so` files, fonts, or
game data.

`portmaster/build_port.sh` creates a clean `nfsmw.zip` containing only the
compatibility runtime and setup metadata. On the handheld, setup hash-checks
the user-provided APK/OBB and extracts the five libraries locally; the OBB is
read directly and never expanded.

## Contributing

Bug reports and focused patches are welcome. Please read
[CONTRIBUTING.md](CONTRIBUTING.md), especially the rules against uploading
game files or extracted native libraries.
