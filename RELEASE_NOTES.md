# v0.1.0-alpha

This first public alpha makes the current playable R36S port available for
community testing and help with its final touch-first menu blocker.

Release archive SHA-256:
`3f139a3eb68f00c9b9753a074d1a9bb99578061573553db2ebd5488d6ef70fb1`

## What works

- Boots the supported Android 1.3.128 ARMv7 release on an R36S.
- Runs races at 640x480 with high visual settings.
- Measured 48.84 FPS over a full Run 27 session with only minor local drops.
- Smooth analog steering and D-pad menu navigation.
- A/B, L1/R1, Start, top-level menu navigation and pause behavior.
- Working sound effects through the FMOD/OpenSL-to-SDL bridge.
- Multiple races and career progression validated on physical hardware.
- Clean PortMaster setup that verifies user-supplied game files and extracts
  required libraries locally.

## Known limitations

- The settled pre-race car-selection/purchase screen cannot focus Continue or
  Buy with the controller. Use the quick-A workaround in
  [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
- Soundtrack playback is disabled to avoid a decoder retry loop that cuts FPS
  roughly in half.
- Select's experimental cursor moves, but its taps are rejected by the game.
- Only the R36S on ArkOS has been validated.

## Legal

No APK, OBB, extracted game library, asset, audio, or save is included. Users
must provide the exact supported APK and OBB from their own legally obtained
copy. The MIT license applies only to the compatibility code and packaging.
