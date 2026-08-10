# Second opinion — car-selection screen (runs 30–32)

A second agent took three hardware cycles at the `car_select_new` defect after
run 29. All three failed. The card is back on the **Run-29 mapper**
(`68c2fccb7fb71238bce4a2c266be30cb223855a3a70c84d31b091982aabbf628`, build ID
`6e9de3b982ba449f69ac44e5b8804c1e1fbc9bff`). Sources are in `run30/`, `run31/`,
`run32/`; traces in `run3*/hardware-results/`.

This file records what is now *known*, so the next attempt does not re-tread it.

## The finding that matters

`CarSelectionWidget` drives both car screens. The layout string block in
`published/layouts/layouts.sb` lists their MOGA highlight targets:

| screen | highlight targets |
|---|---|
| `car_select_loadout` | `mogaHighlightMod1`, `Mod2`, `Paint`, **`mogaHighlightButton`** |
| `car_select_new` | `mogaHighlightCar`, `mogaHighlightClass` |

The modifications screen exposes its action button to the highlight ring, so
D-pad + A reaches Race. **`car_select_new` has no `mogaHighlightButton`**, so
`btn_car_action_large` (Continue/Buy) is not a controller-reachable target in
this build. That single asymmetry explains why this one screen misbehaves while
everything else works.

`published/flow/menus/garage/garage_select_car.sb` confirms the flow side. Its
complete output list is: CONTINUE, SKIP_ROLLOUT, FREELOOK, BACK, STORE,
SHOWROOM, OPTIONS, RECOMMENDS, CARUNLOCKED, GAMESCOM_CONTINUE,
TUTORIAL_CONTINUE, the tutorial/purchase/cash/connection prompts, T5SDK_POPUP,
GLOBAL_POPUP. There is **no `CLASS_FILTER_*` output of any kind** — the class
dropdown the layout draws is dead in this build. `CONTINUE` is the one that
advances to `garage_select_rollout`.

Conclusion: this is a limitation of the title's own MOGA support for a
touch-first screen, not an input the mapper withholds.

## Disproved on hardware — do not retry

| run | theory | result |
|---|---|---|
| 30 | Right-stick focus channel (MOGA `AXIS_Z` → codes 190/191 → menu actions 1054/1055), bound to L2/R2 | Inert. A constant scan of the screen class over `0x163000-0x16b000` finds only action ids 1002/1003/1004/1005 — 1054/1055 can never reach it. |
| 31 | Pointer-mode guard `ui_state()+0x239` (touch vs gamepad) | Already 0 for the whole session. Never the blocker. |
| 32 | Force controller presence bytes `0x133`/`0x135` | Made the class dropdown navigable (`CLASS_FILTER_MUSCLE/_SPORTS/_GT` appeared, not just `_SUPER`) but A still never reached Continue, **and locked cars became selectable**. Reverted. Save backed up to `files/var/nfstr_save.sb.pre-run32-backup`. |

## Timing evidence (why "quick A" works)

Across runs 29–32, A on a settled `car_select_new` fired `CLASS_FILTER_*`
**every single time** — including presses made immediately after a D-pad Right
that had already played `swipe_change`. The carousel picture moves; the
highlight never does.

A advances (`btn_accept`) only inside roughly the first 50 frames after the
layout loads — measured at 9, 38 and 47 frames succeeding; 65, 92, 122, 190,
198, 215 and 254 frames failing. The boundary is the rollout settling, not any
input. One failing case had no D-pad press at all beforehand.

## Working workaround

**B to leave the car screen, A to re-enter, then A again within ~1 second.**
Succeeded on every attempt across all four traces (~100 times in the run-32
log). Worth documenting in the port README and controls.

## The one avenue left

The screen was built to be tapped, and the mapper already injects taps via the
Select cursor — they are silently discarded.

`Java_com_ea_ironmonkey_GameGLSurfaceView_nativeTouchScreenEvent` (`0x54d764`):

- signature is `(JNIEnv*, jobject, jint action, jint pointerId, jfloat x, jfloat y)`
  with the floats on the stack — the mapper's `NFSMW_SOFTFP` declaration is correct;
- it takes **pixel** coordinates and divides by screen width/height internally,
  so `640x480` coordinates are right;
- it resolves its view by walking the registered-view list at `0x54a244`,
  comparing each entry against the passed object with `env->IsSameObject`
  (JNIEnv slot `0x60`);
- on failure it logs `AndroidInput: Unregistered view calling nativeTouchEvent:`
  and returns. **The run-28 trace shows this on every synthetic press**, which
  is why run 28's direct-touch shortcut at 580,425 did nothing.

So the mapper passes `&activity` where a registered `GameGLSurfaceView` object
is expected. Making that lookup succeed would fix this screen and any future
touch-only screen. That work was not attempted — it means finding what
populates the list and either registering a view or returning a matching object
from `IsSameObject`.

Note there is a second early-out at the top of `nativeTouchScreenEvent`: a
global byte loaded through a GOT slot, `!= 0` → immediate return. It was not
resolved and should be checked before assuming the view lookup is the only gate.

## Guest field reference (verified by disassembly)

MogaController singleton — accessor `app_base + 0x3f7c88`:

| offset | meaning |
|---|---|
| `0x132` | input mode byte: 0 = front end, non-zero = racing. Selects the `MogaKeyCode` table at `0x5449a0`. |
| `0x133` | controller connected. Only writer is `setConnected` (`0x7913c`), which returns early unless `0x135 != 0`. |
| `0x134` | second presence flag, checked as `0x133 | 0x134` |
| `0x135` | MOGA available. Written once at startup (`0x547208`) from a JNI probe the bounded JNIEnv cannot answer, so the mapper's three state events are discarded. |
| `0x138` / `0x13c` | `AXIS_X` / `AXIS_Y` floats — the proven direct steering path |

UI state singleton — accessor `app_base + 0x7566c`:

| offset | meaning |
|---|---|
| `0x238` | right-stick-Y pseudo-button latch (`nativeOnMotionEvent`) |
| `0x239` | touch (1) vs gamepad (0) mode; gates the car screen's D-pad handlers |

`MogaKeyCode` (`0x5449a0`), front end (`0x132 == 0`) vs racing (non-zero):

```
D-pad 19/20/21/22 -> 1004/1005/1002/1003 | racing: 0
A  96  -> 1046   | racing: 1027      B  97  -> 1053 | racing: 0
X  99  -> 1050   | racing: 0         Y 100  -> 1051 | racing: 0
L1 102 -> 1048   | racing: 1028      R1 103 -> 1049 | racing: 1026
START 108 -> 1052 | racing: 1053
right stick X -> 190/191 -> 1054/1055 | racing: 0
right stick Y -> 188/189 -> 0         | racing: 1056/1057
L2/R2 (104/105) and stick clicks (106/107) map to nothing at all.
```

Car-select D-pad handler guard chain (`0x165f08`, `0x166038`, `0x166138`,
`0x166274`), all four identical:

```
if (this->busy_0x94)                    return;
if (this->targets_begin == targets_end) return;
if (this->widget_0x150->flag_0x104)     return;
if (ui_state()->byte_0x239 != 0)        return;   // ruled out, always 0
this->carousel_index_0x18c += step;
```

`showMogaHighlight` for this screen (`0x167a14`) picks between
`mogaHighlightClass` and `mogaHighlightCar` from `this+0x18c`, and returns
immediately if `(controller->0x133 | controller->0x134) == 0`.

## Build note

The runtime was rebuilt with the Homebrew `arm-unknown-linux-gnueabihf` GCC
15.2.0 toolchain at
`/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/bin`, via
`run3*/build_and_install.sh`. A Zig/clang cross-build also compiles the tree
clean under the project's full `-Werror` set and honours
`__attribute__((pcs("aapcs")))` correctly, but LLD does not emit
`Tag_ABI_VFP_args: VFP registers`, so it should not be shipped.
