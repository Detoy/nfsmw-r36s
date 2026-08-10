# Known issues and community help wanted

## Pre-race car selection and purchase

This is the one known progression blocker in `v0.1.0-alpha`.

After the `car_select_new` rollout animation settles, A activates the currently
focused car-class control instead of the visible Continue or Buy action. D-pad
navigation cannot focus `btn_car_action_large`. The later
`car_select_loadout` modifications screen works correctly with D-pad and A.

### Working workaround

Press B to leave the car screen, press A to re-enter it, then press A again
immediately—within roughly one second, before the rollout settles. This worked
on every attempt in hardware runs 29–32.

### What has already been ruled out

- Run 28: a direct synthetic touch reached the JNI entry point but was rejected
  because its view was not registered.
- Run 29: removing duplicate D-pad-as-stick delivery fixed the modifications
  screen, but not `car_select_new`.
- Run 30: MOGA right-stick focus actions 1054/1055 do not reach the screen.
- Run 31: the title's touch/gamepad mode byte was already in gamepad mode.
- Run 32: forcing MOGA controller-presence bytes exposed class navigation and
  locked cars, but still did not expose Continue/Buy; the change was reverted.

The evidence indicates that this Android build exposes `mogaHighlightCar` and
`mogaHighlightClass` on `car_select_new`, but no `mogaHighlightButton` for its
Continue/Buy action. Its flow declares `CONTINUE`, while the settled A emits an
unhandled `CLASS_FILTER_*` output.

### Most promising remaining direction

Make touch injection use a view object accepted by
`GameGLSurfaceView_nativeTouchScreenEvent`. The native function walks the
title's registered-view list and checks object identity; current synthetic taps
log `AndroidInput: Unregistered view calling nativeTouchEvent` and are dropped.
Solving this generically should also cover later touch-only screens.

Please include the following with a report or pull request:

- handheld and firmware;
- mapper build ID and SHA-256;
- `ports/nfsmw/logs/nfsmw.log` from a short reproduction;
- exact screen and button sequence;
- whether the quick-A workaround still works.

Do not upload an APK, OBB, extracted `.so` library, save file, or other
copyrighted game data.

## Soundtrack

Sound effects work. Music is disabled because the Android decoder fails and
retries continuously on Linux, roughly halving frame rate. A bounded decoder
replacement or FMOD interception that preserves the current SFX path is
welcome.

## Experimental cursor

Select toggles a development cursor, but taps are rejected for the same
unregistered-view reason described above. It is not a usable control method in
this alpha.
