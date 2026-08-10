# Need for Speed: Most Wanted for R36S

An experimental compatibility port of the 2012 Android release of *Need for
Speed: Most Wanted* for the R36S and ArkOS.

![Gameplay on R36S](portmaster/nfsmw/screenshot.png)

> **Public alpha:** races are playable with working controls and sound effects.
> One pre-race car-selection action still requires a timing workaround. See
> [Known issues](KNOWN_ISSUES.md) before installing.

This repository contains only independently written compatibility code and
PortMaster packaging. It does not contain the game, an APK, an OBB, extracted
Android libraries, or other Electronic Arts assets.

## What works

- 640×480 OpenGL ES 2 rendering on the Mali-G31
- High visual settings at an observed 48.84 FPS average
- Smooth analog steering and D-pad menu navigation
- Race, map, garage and modification-screen controls
- Sound effects through the FMOD/OpenSL-to-SDL audio bridge
- Career saves and multiple completed races
- Clean exit back to PortMaster

Soundtrack playback is disabled because the original Android decoder enters a
retry loop on Linux and substantially reduces performance.

## Installation

1. Download
   [`nfsmw-r36s-v0.1.0-alpha.zip`](https://github.com/Detoy/nfsmw-r36s/releases/tag/v0.1.0-alpha).
2. Install it through PortMaster, or extract it at the root of the ROMs card.
3. Copy your legally obtained APK and OBB to `ports/nfsmw/gamedata/`.
4. Launch the port. First-run setup verifies the files and extracts the five
   required ARMv7 libraries locally.

### Supported game version

| Item | Value |
|---|---|
| Package | `com.ea.games.nfs13_row` |
| Version | `1.3.128` (`1003128`) |
| APK SHA-256 | `bfbe9d08165b8e976924e94879b40ac6575108d5b92521ca837175c0b291c7c7` |
| OBB filename | `main.1003128.com.ea.games.nfs13_row.obb` |
| OBB SHA-256 | `66dd4e695e698929f789e7c825eabe3ba5a50ed2ce28b628c96e5dbc008043a1` |

Files from other releases and similarly named repacks are not compatible.

## Controls

| Control | Action |
|---|---|
| Left stick | Steering and map movement |
| D-pad | Menu navigation |
| A | Accept / drift |
| B | Back |
| L1 | Brake / reverse |
| R1 | Nitrous |
| L1 / R1 in menus | Change top-level section |
| Start | Pause / select map event |
| Select + Start | Exit to PortMaster |

On the pre-race car-selection or purchase screen, use the workaround described
in [KNOWN_ISSUES.md](KNOWN_ISSUES.md). The following modifications screen works
normally with D-pad and A.

## Development

The runtime maps the original Android ARMv7 libraries, provides the required
Bionic and JNI compatibility surface, translates the Android soft-float ABI,
and hosts graphics, controller and audio output through Linux/SDL.

To prepare a private local test directory from your own game files:

```sh
tools/extract_nfsmw.sh \
  /path/to/your-game.apk \
  /path/to/main.1003128.com.ea.games.nfs13_row.obb \
  gamefiles
```

Build the hard-float ARMv7 runtime with:

```sh
make -C runtime
```

Create the clean PortMaster archive with:

```sh
portmaster/build_port.sh
```

Further technical details are in [PORTING_STATUS.md](PORTING_STATUS.md) and
[research/abi-contract.md](research/abi-contract.md).

## Contributing

Reports and focused patches are welcome. The main controller blocker is
tracked in [issue #1](https://github.com/Detoy/nfsmw-r36s/issues/1). Please read
[CONTRIBUTING.md](CONTRIBUTING.md) before submitting logs or code.

## License and game ownership

The compatibility code and packaging are available under the [MIT License](LICENSE).
The license does not cover *Need for Speed*, its code, data, artwork, audio, or
trademarks. Those remain the property of their respective owners.
