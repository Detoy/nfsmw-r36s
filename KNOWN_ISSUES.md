# Known issues

## Pre-race car selection and purchase

After the car-selection animation settles, A changes the car class instead of
activating the visible Continue or Buy action.

### Workaround

Press B to leave the screen, A to re-enter, then A again immediately—before the
animation settles. The later modifications screen works normally.

### Developer notes

The screen exposes `mogaHighlightCar` and `mogaHighlightClass`, but not a MOGA
highlight for `btn_car_action_large`. Synthetic taps currently reach the JNI
entry point but are rejected because the supplied view is not registered.
Making touch injection use the title's registered view is the leading fix.

Technical discussion is tracked in
[issue #1](https://github.com/Detoy/nfsmw-r36s/issues/1).

## Music

Sound effects work. Music is disabled because the original Android decoder
fails and retries continuously on Linux, causing a large performance loss.

## Experimental cursor

Select toggles a development cursor, but taps are rejected by the game. It is
not a functional control method in this alpha.

When reporting a problem, include your handheld, firmware, release version,
exact reproduction steps and the relevant part of `ports/nfsmw/logs/nfsmw.log`.
Do not upload game files, extracted libraries, assets, audio, or saves.
