# Porting status

Updated: 2026-08-10

## Current state

The R36S public alpha is playable. It boots the supported Android ARMv7 build,
renders full races, accepts analog and digital controls, saves career progress,
and outputs sound effects. The current release is
[`v0.1.0-alpha`](https://github.com/Detoy/nfsmw-r36s/releases/tag/v0.1.0-alpha).

| Area | Status |
|---|---|
| ARMv7 loader and relocation | Pass on R36S |
| JNI and Android compatibility | Pass for gameplay |
| OpenGL ES 2 rendering | Pass at 640×480 |
| Race controls | Pass |
| Front-end controls | Partial; see car-selection issue |
| Sound effects | Pass |
| Music | Disabled |
| Performance | 48.84 FPS measured average |
| PortMaster packaging | Public alpha |

## Validated technical milestones

- All five bundled ARMv7 modules load on the physical handheld.
- All 56,142 dynamic relocations resolve.
- All guest constructors required for startup complete.
- The compatibility audit covers 84 imported Bionic/Android boundary APIs.
- All 49 imported scalar-float APIs use ARM soft-float thunks.
- The OBB reader indexes all 2,411 entries without expanding the archive.
- GLES2, SDL controller input and SDL/ALSA output run through the R36S Linux
  graphics and audio stack.
- A full high-graphics test session rendered 16,277 frames in 333.265 seconds:
  **48.84 FPS average**, with working sound effects.
- Multiple tutorial and career races have been completed on real hardware.

## Release identity

- Mapper SHA-256:
  `68c2fccb7fb71238bce4a2c266be30cb223855a3a70c84d31b091982aabbf628`
- Mapper GNU build ID: `6e9de3b982ba449f69ac44e5b8804c1e1fbc9bff`
- Alpha archive SHA-256:
  `3f139a3eb68f00c9b9753a074d1a9bb99578061573553db2ebd5488d6ef70fb1`

The release archive contains no APK, OBB, extracted Android library, game save,
or other proprietary game payload.

## Remaining work

### Car-selection Continue / Buy action

The touch-first `car_select_new` screen does not expose its Continue/Buy button
as a MOGA highlight target after the rollout settles. A during the rollout
works; afterward it activates the class filter instead.

The most promising fix is to make synthetic touch input use a view object
accepted by `GameGLSurfaceView_nativeTouchScreenEvent`. Current taps are dropped
with `AndroidInput: Unregistered view calling nativeTouchEvent`.

Approaches already ruled out:

- duplicate D-pad/analog delivery;
- MOGA right-stick focus actions;
- forcing the title's pointer-mode flag;
- forcing controller-presence bytes.

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and
[issue #1](https://github.com/Detoy/nfsmw-r36s/issues/1).

### Music

The original Android music decoder fails and retries continuously on Linux,
roughly halving performance. Music remains disabled while the working sound
effects path is preserved.

### Wider device testing

Only the R36S running ArkOS has been validated. Other ARMv7 PortMaster devices
may need graphics-driver, controller-map or memory adjustments.
