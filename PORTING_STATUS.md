# Porting status

Updated: 2026-08-10

## Decision

Proceed with a public alpha. The core port is playable on physical R36S
hardware: multiple races complete, smooth analog steering works, sound effects
work, and Run 27 averaged 48.84 FPS at the high visual tier. The remaining
progression defect is isolated to the touch-first pre-race car-selection and
purchase action; the quick-A workaround is reliable but not release-quality.

## Evidence established

- The real APK is package `com.ea.games.nfs13_row`, version `1.3.128`.
- `libapp.so` is an 11 MB stripped ARM EABI5 module built for ARMv7/Thumb-2,
  VFPv3 and NEON.
- It has 540 unique imports and 49,048 dynamic relocations.
- Its bundled dependency set is `libc++_shared.so`, `libfmodex.so`,
  `libfmodevent.so`, and `libNimble.so`, plus Android/GLES system libraries.
- All 63 direct FMOD imports made by `libapp.so` are exported by the bundled
  FMOD pair. Reusing those libraries is therefore the primary audio plan.
- The FMOD core dynamically looks up `slCreateEngine` and the OpenSL interface
  IDs. Linux needs an OpenSL-compatible output bridge or an intentional
  no-audio failure mode.
- The OBB is a ZIP containing 2,411 files. It is about 583 MB on disk but
  expands to 2,096,371,491 bytes, so the runtime should mount/read it as an
  archive rather than expand it on the SD card.
- Controller-native entry points exist for MOGA key, motion, and state events.
- Startup and rendering have clear JNI entry points: `nativeOnCreate`,
  `nativeSurfaceCreated`, `nativeSurfaceChanged`, and
  `nativeOnRunLoopTick`.
- Gate G2 passed on the physical R36S: all five modules mapped and their
  dynamic tables were inspected without guest execution.
- The latest target provider census covered 982 imported symbol occurrences:
  254 resolve from bundled guest modules, 61 use prepared soft-float thunks,
  496 have target-host candidates, 169 cross Bionic/Android compatibility
  bridges, and two census-only aliases (`__assert2`, `__dso_handle`) are
  supplied explicitly during relocation.
- Independent host preflights passed at 640x480: Mali-G31 GLES through SDL
  KMSDRM, the `GO-Super Gamepad`, and 44.1 kHz stereo SDL/ALSA output.
- Relocation phase A now implements `R_ARM_RELATIVE`, `R_ARM_ABS32`,
  `R_ARM_GLOB_DAT`, and `R_ARM_JUMP_SLOT`, restores final segment protections,
  flushes the instruction cache, binds guest-module exports in load order, and
  leaves unresolved ABI-boundary entries untouched. Constructors remain off.
- Relocation phase A passed on the physical R36S: all 56,142 relocations were
  structurally processed, 55,991 were written, 151 ABI-boundary relocations
  were intentionally preserved, and there were zero missing providers.
- The compatibility build now covers all 84 imported ABI-boundary APIs with
  ARM32 layouts for Bionic pthreads/semaphores, 84-byte `FILE` sentinels,
  Android `stat`/`dirent` translation, logging, bitmap failure semantics,
  ctype tables, signal actions/masks and native ARM jump buffers.
- Full relocation passed on the physical R36S. All 56,142 relocations were
  applied with zero unresolved entries: 3,043 guest-module resolutions, 59
  soft-float resolutions, 478 host resolutions and 159 compatibility/alias
  resolutions. There were no blocked or missing providers.
- The sixth hardware run completed 231 of 234 constructors, then deliberately
  aborted in the old fail-loud `sigsetjmp` stub at app initializer 186. The
  preserved trace proves all libc++, FMOD, Nimble and app initializers 0–185
  returned cleanly.
- The replacement signal bridge passed its target-side self-test. All 233
  non-sentinel constructors and all five non-sentinel ELF finalizers then
  returned cleanly on the R36S. The subsequent signal 11 occurred only after
  finalization during host teardown; successful guest runs now exit directly
  instead of unmapping code that may still have Android callbacks registered.
- The staged JNI build supplies JavaVM `GetEnv`, reference/class lookup and
  method-ID registration for the calls statically visible in `JNI_OnLoad` and
  `nativeOnCreate`. Every other JNI table slot fails loudly with its guest
  return address. OBB access remains disabled.
- The eighth hardware run reached `JNI_OnLoad` cleanly. It requested JNI 1.2,
  which the initial bridge intentionally rejected because it accepted only
  1.6; shutdown and all finalizers remained clean. The staged correction
  accepts the stable JNI 1.2, 1.4 and 1.6 table versions while requiring this
  title's `JNI_OnLoad` result to be 1.2.
- To minimize further card cycles, the next build is a consolidated gameplay
  build rather than another single-call gate. Its fake JavaVM now supports
  thread attach/detach, exception checks, references, strings, arrays, fields,
  instance/static calls, the activity/run-loop services, device/display
  values, and the exact OBB-mode booleans/path expected by this release.
- A local OBB index test passed against the real 583 MB archive: all 2,411 ZIP
  directory entries were indexed in place, known file sizes and directory
  sentinels matched, and root listings were returned without extracting the
  2.1 GB payload.
- The persistent runtime now retains one 640x480 GLES2 context across
  `nativeOnCreate`, surface creation/change, resume, and repeated
  `nativeOnRunLoopTick` calls. SDL controller transitions are dispatched to
  the game's physical-key JNI entry points; Back+Start exits the test cleanly
  and the log records frames, elapsed time, and measured FPS.
- The old FMOD/OpenSL blocker has an initial real implementation: guest
  `dlopen`/`dlsym` calls for `libOpenSLES.so`, `slCreateEngine`, and the five
  interface IDs are intercepted; engine, output-mix, player, play,
  configuration and Android simple-buffer-queue interfaces feed copied PCM
  buffers to SDL/ALSA. This path compiles for ARMv7 but remains target-audio
  unverified.
- The consolidated mapper passes both ARM toolchains with warnings-as-errors,
  the 84-function Bionic compatibility audit, the 49-function soft-float
  audit, and the real-OBB archive test. Build ID and SHA-256 are recorded when
  the final staged binary is copied to the card.
- Consolidated correction candidate: GNU build ID
  `2ac54edc86289fefd134b782776369a9e358e5f6`, mapper SHA-256
  `5a9113a2aa4a47c8c55a773733f04cfd91906d56718fac68b07e18043121b302`.
  The clean legal PortMaster archive is 167,329 bytes with SHA-256
  `fb23f75edc73b018b80eb2f9543c6b72463f2c28ed6354cac3c9b13619a3f71b`;
  its setup path was tested end-to-end and reproduced all five pinned library
  hashes from the supported APK.
- The staged candidate also sends the complete Android lifecycle sequence:
  start/resume before surface activation and pause/stop/destroy after the
  interactive loop, while the GLES context is still current.
- Hardware run 9 passed `JNI_OnLoad`, `nativeOnCreate` (56 registered Java
  methods), the 2,411-entry OBB index, persistent 640x480 GLES creation, the
  full surface lifecycle and entry into the first real game tick. The title
  identified the Mali-G31 and initialized its GLES extension layer before
  failing in `NimbleWrapper::InitNimble`. The preserved trace is
  `hardware-results/gameplay-run9-nimble-view-zero.log`, SHA-256
  `8bc5d6ed8a51393c18a14bda7502d6e1976f47e41d528eb58210e95828141c6a`.
- Run 9 also exposed a deterministic JNI contract error: the game's internal
  helpers call the `Call*MethodV` table variants, which were still neutral.
  Consequently device strings, memory/performance values and the primary
  view dimensions were read as empty/zero; the trace explicitly reports
  `Primary view size: 0  0`. The correction implements instance and static
  direct/V/A variants for object, boolean, integer, long, float, double and
  void calls, field setters, usable font metrics, and fixed 640x480 view
  queries. A target crash reporter now records signal address, PC, LR and SP
  if a later first-tick failure remains.
- The user explicitly approved replacing the two pending card files. The
  corrected mapper and launcher are now staged and re-verified on-card; the
  large OBB and five pinned Android libraries were left untouched.
- Hardware run 10 verified the JNI V/A correction completely: device, locale,
  version, density, `768 MB` memory, performance tier and the primary
  `640x480` view all reached native code correctly. The crash reporter then
  captured a null dereference at `libNimble.so + 0xF2C`, LR
  `libNimble.so + 0xF6D`. Disassembly proves this is `EA::Nimble::getEnv()`
  dereferencing its saved JavaVM. The preserved trace is
  `hardware-results/gameplay-run10-null-host-pc.log`, SHA-256
  `b82b138ec3d561262c2410cf2a20c8c488ed932bbd4103e1f3d0341cb6b67acd`.
- Android invokes `JNI_OnLoad` once for every loaded JNI library. The manual
  loader had invoked only the app copy, leaving Nimble's JavaVM global null.
  The next candidate calls `libNimble.so` `JNI_OnLoad` first (requiring its
  JNI 1.6 result), then calls the app `JNI_OnLoad` (JNI 1.2), matching Android
  dependency order before `nativeOnCreate`.
- The Nimble JNI correction is staged and hash-verified on the attached card.
  Only the 382 KB mapper changed; the existing legal game data remains intact.
- Hardware run 11 verified the Nimble correction and advanced through Nimble
  initialization, telemetry setup, locale setup, and mounting the `texture_etc`,
  `1x`, and `2x` SKUs. It then exited with signal 6 while constructing the
  game's fonts. The preserved trace is
  `hardware-results/gameplay-run11-font-abort.log`, SHA-256
  `896be20f1dd9d0ac375df3b92e1d36fd2cd074c9ff341e54667a33b80e403914`.
- Run 11 deterministically exhausted the bridge's 128-entry JNI field registry:
  the trace contains exactly 128 field queries, including 24 complete repeats
  of the same five `Paint.FontMetricsInt` fields and the 25th `ascent` query.
  Method IDs were already deduplicated, but field IDs were not. The correction
  now reuses identical field IDs and reports the guest return address for any
  future `abort` or `__assert2` failure.
- Font-registry correction candidate: GNU build ID
  `ee795272705b5248456b498f40d1018a2fbb9e94`, mapper SHA-256
  `72c91d2bb531fafa4ad41b477e3dec6347ac072827d67885dc29857ed9c4877e`.
  Both strict ARM toolchains pass, the real 2,411-entry OBB test passes, and the
  rebuilt clean PortMaster archive has SHA-256
  `a36a87a09a9866ceb71e52d531cd5e815602830d0864edd757c21c888e6b7fbf`.
- That field-only build was superseded before the next hardware run. Static
  analysis of the immediately following text-upload routine proved it calls
  `AndroidBitmap_getInfo`, `lockPixels`, and `unlockPixels` and does not check
  their return values; the old deliberate-failure bitmap stubs would therefore
  have produced a null pixel copy. The consolidated correction creates bounded
  RGBA8888 bitmap objects, supplies width/height/stride and lockable pixels,
  implements clear/draw operations, and includes a compact 5x7 ASCII fallback
  rasterizer so dynamic English text remains visible without Android Canvas.
- Final run-12 candidate: GNU build ID
  `f2ebae69a4ac6a40590fa1d1da28dbda8d39d845`, mapper SHA-256
  `0b3505b320fe44fd0cf2dedb47f79a9f5fc407d62f2989e579d5eeee4928e940`.
  It contains an on-device RGBA/stride/lock/fallback-font self-test before
  guest JNI execution. The clean PortMaster ZIP SHA-256 is
  `bf418b9ba091a1717e5da5b11d0a36ab5c8758d1064e5e83ac943a34d6504e57`.
- The final run-12 mapper is staged and SHA-256/build-ID verified on the
  attached card. The launcher, OBB, and five original Android libraries were
  left untouched.
- Hardware run 12 booted the game to its rendered terms/policies screen and
  completed 5,494 live frames in 93.927 seconds (`58.49 FPS`) before the user
  invoked the exit chord. Mapping, relocation, constructors, both JNI loaders,
  OBB access, bitmap upload, lifecycle shutdown, and all finalizers remained
  clean. The preserved trace is
  `hardware-results/gameplay-run12-boot-input.log`, SHA-256
  `4bd62b6b0459b7f92ff457570a9e1004525efeff63915529aa9e97ae3ccb2e2d`.
- Run 12 received every tested R36S face, shoulder, D-pad, Select, and Start
  transition through SDL. The physical Android-key path made Start highlight
  and activate the first consent option, but the title expects controller
  navigation through `MogaController_nativeOnKeyEvent`; other buttons did not
  operate the menu through the keyboard path.
- The accept action also reached the flow engine, which emitted
  `NO_CONNECTION_PROMPT`. Nimble's `getStatus().ordinal()` was receiving the
  neutral value `0` (`UNKNOWN`) from the Java stand-in, and the old mobile flow
  has no valid output from that prompt in this build.
- Run-13 input/network candidate now returns Nimble `Network.Status.OK`
  (`ordinal=3`), reports a connected MOGA-Pro through its state callback, sends
  all digital transitions through the real MOGA key callback, and forwards
  both sticks and triggers through the MOGA motion callback. GNU build ID is
  `195dca424ad1adcb529f3dbd9bd318b01895fba0`; mapper SHA-256 is
  `c0bd171ff94503a1ec6c163fd5813105281099612e6f75dfc8a5b502e6dc852f`.
  The rebuilt clean PortMaster ZIP SHA-256 is
  `dfe940884a8452b5b6b1638454b4ff681fa9fae006c67c426e9e6bd9eb8445bd`.
- The run-13 mapper is staged and SHA/build-ID verified on the attached card;
  only the mapper changed.
- Hardware run 13 verified that A now reaches the game's MOGA listener and
  activates `btn_generic_accept`. The previous `NO_CONNECTION_PROMPT` never
  appeared, confirming the Nimble `Network.Status.OK` correction. The process
  remained stable for 2,695 frames in 46.721 seconds (`57.68 FPS`) and exited
  cleanly. The preserved trace is
  `hardware-results/gameplay-run13-moga-accept-no-navigation.log`, SHA-256
  `413b22947166f9dcba7c860db29e21543c33dde5167acf469917bb053fe7a1b6`.
- Run 13 also proves D-pad Up/Down and analog motion entered the title's native
  MOGA handlers, but navigation did not visibly change. The APK motion contract
  exposes `getX`, `getY`, `getRawX`, and `getRawY`; the bridge had implemented
  only `getAxisValue`, so the primary stick coordinates still read as zero.
- Run-14 navigation candidate implements every primary stick getter, applies a
  4096-unit dead zone, and mirrors D-pad directions to full-scale left-stick
  pulses. Direct D-pad key callbacks are suppressed to prevent one press from
  traversing both the digital and synthesized-analog menu paths. GNU build ID
  is `26bf2eaa0472f2d24f247ef52ea02ca04440fb10`; mapper SHA-256 is
  `408ba11f8c6dc787dd9421aed6b9566370e5f5c96f714870e7302e6edd857934`.
  The clean PortMaster ZIP SHA-256 is
  `fc52748f517167edbae7cb97427df7b4207816d78e2ae114ef0cbc799331e2ef`.
- The run-14 navigation mapper is staged and hash/build-ID verified on the
  attached card. No game data or launcher file was recopied.
- Hardware run 14 remained on the consent screen, but it resolved the input
  ambiguity. The title logged every face/shoulder transition, primary motion,
  and D-pad mirror through its native MOGA code; A repeatedly selected the
  first `EULA` flow output, called Android `openURL`, and returned to
  `active_accept`. The preserved trace is
  `hardware-results/gameplay-run14-navigation-still-consent.log`, SHA-256
  `fb10e01a71b52aa53efd96e7627f05d3597ddff24b41f75d2618f43f5621fa04`.
- Static disassembly then identified the exact navigation mismatch. NFS MW
  stores the driving stick from MOGA `AXIS_X/AXIS_Y` (0/1), but synthesizes its
  four internal menu keys only when `AXIS_Z/AXIS_RZ` (11/14) reach exact
  end-stop values. Run 14 had mirrored the D-pad onto 0/1, so the trace showed
  valid motion without producing the menu keys needed to move from `EULA` to
  `ACCEPT`.
- Run-15 correction preserves the real left stick on 0/1 and mirrors the
  physical D-pad plus deliberate half-scale left-stick directions onto the
  game's checked 11/14 pair. GNU build ID is
  `64b2722a4db912d92a6b1f259adf4c01224ca1eb`; mapper SHA-256 is
  `725a866e6ce2aabef61c561492c6dcd8a6c3a72fbc5a988d3a5e274252c1b1c2`.
  The rebuilt clean PortMaster ZIP SHA-256 is
  `35e750070da36f727a729952718179e438c8589ef310a14a624b3f6929b6447c`.
  The mapper is staged and SHA/build-ID verified on the attached card; the
  launcher, OBB, and five Android libraries were not recopied.
- Hardware run 15 proves that correction reached the title exactly as
  intended: native MOGA logs now distinguish Up, Down, Left, and Right, along
  with their releases. The legacy `AAScreen` still ignores those navigation
  events, while every A press continues to activate the first `EULA` entry and
  reload `active_accept`. The process remained stable for 5,707 frames in
  97.358 seconds (`58.62 FPS`) and exited cleanly. The preserved trace is
  `hardware-results/gameplay-run15-native-directions-ui-ignored.log`, SHA-256
  `bd725655fa7f7910d19558a05b7e438efa734df90561585b06c3b7f02a90a2c6`.
- Disassembly of the game's `ActiveAccepted` writer and startup checker shows
  that acceptance is represented by `files/active_accepted`; the checker tests
  only whether that path exists. Because the user explicitly attempted to
  accept repeatedly, an empty marker was created and verified on the attached
  card (empty-file SHA-256
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`).
  The next launch should follow the normal `ACCEPT -> update_check` flow
  without entering the obsolete touch-oriented consent screen.
- Hardware run 16 passed the consent marker, played the opening race cutscene,
  entered the first race with its HUD visible, accepted live MOGA analog and
  drift input, opened the pause screen, and exited cleanly after 1,762 frames.
  The preserved trace is
  `hardware-results/gameplay-run16-first-race-black-world.log`, SHA-256
  `233b4d970d25b0fb5949d56a1d6a02fbddcf582f67d8c93c5b24b3f0a97102e9`.
- Run 16 exposed three deterministic problems. The Java stand-in supplied a
  performance score of 100, making the game select `Tier = Higest` and render
  level 4 with puddles, road/car reflections, high-detail assets, and an
  overall 17.19 FPS. The 3D race world became black while the independent HUD
  path remained visible. Finally, failed FMOD `createSound` calls caused
  `MusicManager::PlayNextTrack()` to cycle the entire playlist repeatedly,
  explaining the rapidly changing song display and adding avoidable load.
- The run-17 consolidated candidate defaults to an adjustable performance
  score of 20 instead of 100, and reports Android user music as active while
  the FMOD archive path is unavailable. This intentionally silences the title
  music manager and prevents its per-frame retry storm without removing the
  existing OpenSL work. It also samples GLES errors/framebuffer completeness
  so another black-world result will identify a concrete graphics failure.
- Because MOGA controls work while driving but the legacy pause UI remains
  touch-oriented, run 17 adds a controller-driven touchscreen cursor. Start
  enables/disables a visible yellow crosshair, D-pad moves it, and A taps its
  position while all original MOGA events continue to be delivered. The touch
  call uses the guest Android soft-float ABI.
- Run-17 candidate GNU build ID is
  `eda22860557d1aa13d08063b89bfd2b4e17d5a95`; mapper SHA-256 is
  `58a09736327a3ca3ada87e3d8b758f8e59d73f502b5ddb0a922c82e3cbe5c3a4`.
  Both strict ARM toolchains, the 84-function compatibility audit, the
  49-function soft-float audit, and the real 2,411-entry OBB test pass. The
  clean PortMaster ZIP SHA-256 is
  `7b21bbc6eb7b87d2cd450cc6daa5d5fed469e3c8947734c6be08d218ab260b73`.
- The run-17 mapper and mapper-test launcher are staged and verified on the
  attached card. The on-card mapper hash/build ID match the candidate above;
  the launcher SHA-256 is
  `4e7fa89500300910a8863004b7507bae6ac3979ebeed711e7df183fab1b4ddeb`
  and explicitly sets score 20 plus silent-audio mode. The OBB, Android
  libraries, consent marker, and saved data were left untouched.
- Hardware run 17 rendered the opening race at a user-confirmed playable rate
  and averaged 49.16 FPS over 4,487 frames. L1 reached Brake, R1 reached Nitro,
  the mobile control scheme auto-accelerated, Start opened the pause menu, and
  MOGA A opened its highlighted Controls entry. The preserved trace is
  `hardware-results/gameplay-run17-low-tier-touch-cursor.log`, SHA-256
  `72f8c5bf23a429d4ad80a12e25860caf5f4b31a3097334b8b9a22bd75e405301`.
- The score override was accepted (`PerformanceScore = 20`) but this release
  still reported `Tier = Higest` and render level 4. The performance gain is
  therefore chiefly attributable to suppressing the failed music loop: the
  title attempted `PlayNextTrack` only at normal transitions and immediately
  stopped because `IsUserMusicPlaying()=true`. Tier selection needs separate
  reverse engineering, but it is no longer blocking playability.
- GLES diagnostics recorded a complete default framebuffer and zero errors on
  every sample, including after the world turned black. The trace instead
  shows each left-stick half-travel being duplicated as an internal MOGA
  `Dpad Left/Right` event; immediately afterward the race reported that it
  could no longer find track positions. The black screen is therefore an
  input/game-state failure, not a Mali framebuffer failure.
- Run 18 keeps analog X/Y exclusively for primary steering and never mirrors
  it onto the title's hidden Z/RZ menu axes. D-pad drives the cursor/menu axes
  only while the cursor is visible; outside menu mode, left/right is merely a
  secondary full-scale steering fallback. Touch A now sends DOWN on physical
  press and UP on physical release instead of queuing both in one frame.
  Moving the analog stick or using a shoulder after resuming automatically
  hides the cursor.
- Run 17 completed the interactive loop, then returned 139 while Android's
  destroy path dereferenced a null FMOD event left by failed `createSound`.
  Run 18 uses process-owned teardown after a successful compatibility session,
  allowing the OS to reclaim guest objects rather than invoking the unsafe
  Android/FMOD destruction graph.
- Run-18 candidate GNU build ID is
  `c0a4b6ef3d60dca2cdb59be9dfb90766e291efa3`; mapper SHA-256 is
  `92570bcaaf2c691bff02a3eb3807e3d9857e1c4b19fb929366ea3c4db40434eb`.
  Both strict ARM toolchains, both ABI audits, and the real OBB test pass. The
  clean PortMaster ZIP SHA-256 is
  `786976ab2df0ed7093657d5b00129d19f17505f754779b2c3873ef5c83886a61`.
- The run-18 mapper is staged and SHA/build-ID verified on the attached card.
  Only the runtime changed; the launcher, OBB, Android libraries, consent
  marker, and saved data were preserved.
- Hardware run 18 confirms the input-isolation correction: both the left
  analog stick and D-pad steer, L1 provides brake/reverse, and R1 provides
  boost. The process ran for 3,069 frames in 63.462 seconds (48.36 FPS) and
  exited cleanly with code 0 through the process-owned teardown. The preserved
  trace is `hardware-results/gameplay-run18-steering-pass-delayed-black.log`,
  SHA-256
  `8d263e0ea42dd599eb982ac519834740de7adcdfee58195933d376e777ee63c9`.
- The same trace contains no A/B/X/Y transitions even though the buttons were
  exercised. The GO-Super Gamepad's shoulders, Start, Select, D-pad, and axes
  do arrive, so this is a face-button translation gap rather than a disconnected
  controller. The first repeated `UpdateSpawning failed to get current track
  position` messages begin near frame 2,530, after the uncompleted scripted
  drift prompt; GLES still reports a complete framebuffer and zero errors.
  The most likely current explanation for the later black world/HUD-only state
  is that the drift tutorial cannot advance without its B event, although the
  next trace must distinguish that from a separate world-state fault.
- Run 19 adds a GO-Super-specific raw SDL joystick fallback using the controller
  database's physical mapping: raw buttons 1/0/2/3 become Android A/B/X/Y,
  with equivalent fallbacks for shoulders, Start, Select, stick clicks, and
  D-pad. Raw changes are logged as `G6-RAW`, while the normal SDL controller
  path remains enabled and is ORed with the fallback. This makes the next
  hardware run both a fix attempt and a complete input diagnostic.
- MOGA `getEventTime()` now returns the runtime's monotonic millisecond tick
  count, matching Android input-event semantics instead of returning epoch
  time. The auto-accelerating mobile scheme also receives neutral left-stick Y,
  retaining analog X steering while excluding incidental vertical travel from
  its driving filter. L1 remains the game's brake/reverse input.
- Run-19 candidate GNU build ID is
  `203c37adf26e5a977cdddb05e744078a52dbedd9`; mapper SHA-256 is
  `cb017f1a86dca2ed31bf40f383f266153d9d34ee5e9d77179be71ccdc1969fa6`.
  Both strict ARM builds, the 84-function compatibility audit, the 49-function
  soft-float audit, and the real OBB test pass. The clean PortMaster ZIP
  SHA-256 is
  `84c5f3a30064c9b61f491ed1a2dfdcc78f53bfd81c2847c79efb4f96a46c1929`.
- The run-19 mapper is staged on the attached card and its SHA-256, 444,664-byte
  size, and GNU build ID were verified in place. No game data or save files
  were replaced.
- Hardware run 19 proves the GO-Super raw fallback and corrected MOGA timing:
  physical A reached Android key code 96, completed the scripted drift prompt,
  and analog steering continued to work. All four face-button raw values and
  translated MOGA key transitions were recorded. The run completed 3,436
  frames in 68.425 seconds (50.22 FPS) and exited cleanly with code 0. The
  preserved trace is `hardware-results/gameplay-run19-face-pass-early-black.log`,
  SHA-256
  `c01dde6aa87ef178cc47d004862e5c131500fd4a0eb1290d629bb7fb1b2a670e`.
- Run 19 still entered the black-world/HUD-only state, earlier than run 18.
  The first `UpdateSpawning failed to get current track position` occurs near
  frame 2,210, followed by failures to locate track and world positions. The
  default framebuffer remains complete with zero GLES errors. Because drift
  completed normally, the tutorial-block hypothesis is rejected; invalid or
  unavailable world/simulation state remains the blocker.
- Native disassembly resolves why score 20 never selected a low tier. The
  title compares its performance score against normalized thresholds near
  1.10, 1.12, 1.32, and 1.60. Both the earlier 100 and current 20 are therefore
  far above the top threshold, selecting native tier value 24, logged as
  `Tier = Higest`, and render level 4. Run 20 uses score 1.0 in both mapper and
  launchers to select the genuine lowest tier while preserving the other
  changes that produced 40+ FPS.
- Run 20 also translates Bionic 32-bit `clock_gettime` and `gettimeofday`
  structures explicitly instead of passing guest buffers to glibc. This
  removes a remaining simulation-time ABI ambiguity. JNI long arrays are now
  implemented for the title's tracking path, eliminating roughly two unknown
  JNI calls per active frame and their logging overhead.
- Run-20 candidate GNU build ID is
  `d4d8ba1554c6d4dde5d077b16904e1616912f80c`; mapper SHA-256 is
  `f07d64abb5cec92f79567a70f0c92d955e742d9f0286ee14945633afca334e1e`.
  Both strict ARM builds, the 84-function compatibility audit, the 49-function
  soft-float audit, and the real 2,411-entry OBB test pass. The clean PortMaster
  ZIP SHA-256 is
  `7fc38cd04475d76b904a4527b62e3e8df4e77873b2729e5e753df17d7ba85bc5`;
  mapper-test launcher SHA-256 is
  `12b107c4d30bf664a23965569aa0463362dd21cc950b4babaddaf3bf20938bb8`.
- The run-20 mapper and mapper-test launcher are staged and verified on the
  attached card. Their on-card hashes and mapper build ID match the candidate,
  and the installed launcher was inspected to confirm
  `NFSMW_PERFORMANCE_SCORE=1`. The OBB, Android libraries, consent marker, and
  saved data were not changed.
- Hardware run 20 confirms the reverse-engineered tier selector: score 1 logs
  `Tier = Low` and render level 0, visibly reducing the title's graphics.
  Controls work and D-pad steering remained stable well into the race. The
  preserved trace is
  `hardware-results/gameplay-run20-low-tier-dpad-pass-analog-black.log`,
  SHA-256
  `b11df71d0002b710c7ebf8328dc82c613e254853c9d0546f3d62ad258b6035d0`.
  It completed 4,002 frames in 73.315 seconds (54.59 FPS) and exited cleanly
  with code 0.
- Run 20 isolates the black-world fault to fractional left-stick motion. D-pad
  steering supplied exact -1/0/+1 MOGA X values through frame 3,635 without
  losing the world. The first later analog value (`-9394`, approximately
  -0.287 after normalization) arrived at frame 3,735; within a few frames the
  title began the same track/world-position failure cascade. The framebuffer
  remained complete and error-free. This rejects graphics tier as the trigger
  and confirms that safe input translation is the required workaround.
- Run 21 quantizes the physical left stick after an 8,192-unit dead zone to
  the exact -32,767/0/+32,767 payload already proven by D-pad steering. The
  player can therefore use the preferred analog stick ergonomically, while
  the title receives only its stable -1/0/+1 MOGA values. D-pad steering is
  retained. Based on the user's preference and Run 19's playable performance,
  score 20 is restored in the runtime and both launchers, returning the full
  reflections/shadows and highest visual tier.
- Run-21 candidate GNU build ID is
  `9cbca24f2a617867fe5e32a27d57c5785b443937`; mapper SHA-256 is
  `2ed35966189f9b6917b40ec6a69f0176b4f8aee89cbfe9a52106f8eaf5244df7`.
  Both strict ARM builds, the 84-function compatibility audit, the 49-function
  soft-float audit, and the real 2,411-entry OBB test pass. The clean PortMaster
  ZIP SHA-256 is
  `c3fb114b8d3be339a7f326376a412b00f17fce71a347abfb818f9e5f287b3f34`;
  mapper-test launcher SHA-256 is
  `4e7fa89500300910a8863004b7507bae6ac3979ebeed711e7df183fab1b4ddeb`.
- The run-21 mapper and mapper-test launcher are staged and verified on the
  attached card. Their hashes and mapper build ID match the candidate, and the
  installed launcher confirms score 20. The OBB, Android libraries, consent
  marker, and saved data were not changed.
- Hardware run 21 is the first complete gameplay proof. The tutorial race was
  completed twice at score 20 (`Tier = Higest`, render level 4), with working
  controls and the restored reflections/high graphics. Across both races the
  log contains no `UpdateSpawning`, track-position, world-position, GLES, or
  framebuffer failures. The preserved trace is
  `hardware-results/gameplay-run21-two-races-complete-postrace-exit.log`,
  SHA-256
  `087200079e8067c61464997dbfbd15bdd9cbd160a75a8b2ef6ca6a254663a056`.
  It reached frame 16,329 over approximately 347 seconds, for an estimated
  whole-session average of **47.06 FPS**. This includes startup, menus, two
  races, both result screens, and loading; the crash prevented the normal
  in-process FPS footer from being written.
- Run 21's only failure is isolated from gameplay and rendering. Pressing A
  for `Next` after the second result entered the loading screen, removed the
  race layer and began unloading assets, then faulted with address `0x8`, PC
  `libapp+0x50eeb4`, LR `libapp+0x311fe8`. Disassembly shows the exact fault is
  `ldr r1, [r0, #8]` with a NULL FMOD Event wrapper. It is an optional-audio
  teardown defect in the silent compatibility path, not the earlier black
  world fault.
- The proposed JNI floating-point ABI explanation was checked at both source
  and machine-code levels. All three `CallFloatMethod` bridges already use
  Android base AAPCS, and the run-21 mapper explicitly moves the hard-float
  result from `s0` into `r0` before returning to guest code (`vmov r0, s0`).
  Therefore the common hard-float/softfp return-register mismatch is not
  present in this bridge. Fractional MOGA values are nevertheless conclusively
  unsafe in the title's downstream path, so they remain excluded.
- Run 22 replaces binary stick quantization with safe pulse-density steering.
  Stick magnitude controls how frequently the mapper sends full steering among
  neutral samples; the game still receives only the exact -1/0/+1 values
  proven stable by run 21, while its own steering smoothing sees a proportional
  time average. D-pad steering continues to send exact full-scale values.
- Run 22 also installs a verified ARM trampoline in libapp's zero-filled RX
  segment padding. The patched FMOD event-state helper returns its normal
  inactive state for a NULL wrapper, and replays the original prologue for
  every non-NULL call. The patch validates the three displaced instructions
  and all cave words before writing, checks both ARM branch ranges, restores RX
  page protection, and flushes the instruction cache.
- Run-22 candidate GNU build ID is
  `557af83b08fc8ce4597ce178ab648e62312edbbb`; mapper SHA-256 is
  `69818c2fc295851ee1a7ca5eda440e0c004108531f0492f8473b2e792c69eaac`.
  Both strict ARM builds, the 84-function compatibility audit, the 49-function
  soft-float audit, native relocation inventory, and the real 2,411-entry OBB
  test pass. The clean PortMaster ZIP SHA-256 is
  `db44f89b86bd7365eed8fa5afa79bf7034676f085e3210fa8a2914de62eb3997`.
- The run-22 mapper is staged and flushed on the attached card. Its on-card
  SHA-256 and GNU build ID match the candidate. The existing verified launcher
  remains score 20, unlimited only when explicitly overridden from its
  18,000-frame test default, and silent-audio mode. OBB, Android libraries,
  consent state, and saved data were not changed.
- Hardware run 22 resumed from the saved post-tutorial map and reached the
  next race without graphics or performance regressions. Horizontal menu/map
  navigation worked, but vertical D-pad navigation did not; race selection
  responded to Start, while car selection through A was inconsistent until
  repeated attempts. Light analog steering was perceptibly pulsed, so temporal
  pulse-density steering is rejected as the final control method. The title
  exited during the race after substantial menu time. The preserved trace is
  `hardware-results/gameplay-run22-menu-pdm-framelimit.log`, SHA-256
  `54210fcdb589921687d6ad62092bd82a9e8eedf5062a1cd3e178df1730df1ced`.
  It proves the exit was exactly the mapper-test launcher's 18,000-frame safety
  limit: the run completed 18,000 frames in 322.020 seconds (**55.90 FPS**),
  printed both normal loop-pass messages, and exited with code 0. There was no
  crash, track/world-position failure, or graphics fault. The limit is removed
  from the next candidate.
- Run 23 supplies smooth analog input without passing a fractional float
  through JNI. The native MOGA handler receives only safe whole X values; the
  mapper then writes the normalized IEEE-754 value directly to the title's
  controller singleton field at offset `0x138`, exactly where disassembly
  shows `nativeOnMotionEvent` storing axis X. This bypasses the suspected
  boundary while retaining continuous steering. The dead zone is reduced to
  4,096 and rescaled so usable travel still spans the full range.
- Run 23 always emits the physical D-pad on MOGA hat axes Z/RZ, restoring all
  four native menu directions. Outside cursor mode, D-pad left/right is also
  mirrored to steering X and up/down to Y as the optional racing fallback.
  This should establish proper menu focus before face-button selection while
  preserving the already working race controls. The mapper-test frame limit is
  now zero; Select+Start remains the explicit exit chord.
- Run-23 candidate GNU build ID is
  `10c5b381e23fb8a1a178b71f7845e26537bda62b`; mapper SHA-256 is
  `3ae88574c1a889d0b0a26e87c12f5f318bb9395dbed9e7babd0268818c7b7ef5`.
  Strict armhf and Android syntax builds, the 84-function compatibility audit,
  49-function soft-float audit, native inventory, and real 2,411-entry OBB test
  pass. The clean PortMaster ZIP SHA-256 is
  `924debf181342599261edf73ef4593a40d25f826576bd64bd74a1dc7d9f23116`;
  mapper-test launcher SHA-256 is
  `f88511cf564abdb1acc70abbbdca84bc0b289872bbc18c58308e956838ab6831`.
- Run 23 is not yet staged: two external-write approval attempts timed out
  before execution, and read-back verification confirms both Run 22 files on
  the card remain unchanged. No partial card update occurred.
- The user manually installed Run 23. Hardware run 23 completed the second race
  with perfectly smooth analog steering and no gameplay, graphics, or world
  faults. The preserved trace is
  `hardware-results/gameplay-run23-smooth-steering-menu-axis-test.log`, SHA-256
  `e0a4c14245fed693aa3ae7e87d6044e565ebb86101a68547c676e1b3212a0fe6`.
  It ran 25,228 frames in 444.718 seconds (**56.73 FPS**) and exited cleanly by
  the Select+Start chord with code 0. Logged direct values span the continuous
  range (including 0.20, 0.50, 0.81, 0.96, and 0.99), proving smooth fractional
  steering works when written after the JNI handler.
- Run 23's remaining failures are confined to menu semantics. Analog left/right
  pans the map and D-pad up/down works, but D-pad left/right changes global menu
  sections. Options, pre-race car selection, and garage car selection cannot be
  navigated consistently. The trace proves the axes are not transposed: the
  game logs physical left as `Dpad Left` and physical up as `Dpad UP`. The
  title consumes MOGA hat left/right as global tab navigation, so emitting the
  hat is the wrong abstraction for active-screen navigation.
- Run 24 leaves MOGA Z/RZ hat axes neutral and mirrors the physical D-pad only
  into the same smooth controller X/Y state used by the analog stick. This
  should make it operate the active map, options list, car carousel, and garage
  rather than top-level tabs; L1/R1 retain their intended shoulder-button tab
  actions. Smooth analog Y is also written directly to controller offset
  `0x13c`, enabling full two-dimensional stick navigation without exposing a
  fractional JNI return. Start no longer toggles the touch cursor. The fallback
  cursor remains available on Select alone for exceptional touch-only screens,
  while Select+Start still exits without toggling it.
- Run-24 candidate GNU build ID is
  `e76d74d9f0ccd0548efb576c21bc2d5c9abf9657`; mapper SHA-256 is
  `be52b28fa7a50bcba178065d442cc726ef17ee178a73284eaf4d27bbbdfa8cb3`.
  Strict armhf and Android syntax builds, the 84-function compatibility audit,
  49-function soft-float audit, and real 2,411-entry OBB test pass. The clean
  PortMaster ZIP SHA-256 is
  `21dd4cf7ee1b61aa758bcbd3dc86df754b4fe9f65aa901961e0562e5cf7ad3b9`;
  the unchanged unlimited mapper-test launcher SHA-256 remains
  `f88511cf564abdb1acc70abbbdca84bc0b289872bbc18c58308e956838ab6831`.
- The Run-24 mapper is installed, flushed, and read-back verified on the card.
  Its SHA-256 and GNU build ID match the candidate. The already-installed
  launcher remains unlimited, score 20/high graphics, and silent-audio mode.
  OBB, Android libraries, saves, and consent state were not changed.
- Hardware run 24 confirms map movement and L1/R1 top-level navigation are now
  correct, but stick-style X/Y still does not navigate highlighted controls in
  Options, Garage, purchase prompts, or the pre-race car carousel. A selects
  the first garage car, but delayed A on the pre-race screen advances to a
  locked Maserati/Lexus instead of confirming. The user completed the purchase
  through the Select-activated touch fallback. The preserved trace is
  `hardware-results/gameplay-run24-active-screen-stick-navigation.log`, SHA-256
  `a1a3652da09268f4886fb5fe844f1aa84a70c9ab9ec6dd799264c37d7dd86bb7`.
  It ended cleanly by the exit chord after 28,829 frames in 571.303 seconds
  (**50.46 FPS**) with exit code 0.
- Native key-handler disassembly identifies the missing submenu input path.
  `nativeOnKeyEvent` has dedicated jump-table cases for Android key codes
  19/20/21/22 and forwards those exact standard D-pad codes. The prior
  MotionEvent path instead synthesized alternate codes 188/189/190/191, while
  run 24 withheld D-pad key events entirely and provided only X/Y state. Thus
  the required standard D-pad key path existed in the mapper table but had
  never been dispatched to the game.
- Run 25 keeps proven direct X/Y state for maps and racing, and additionally
  sends standard D-pad press/release events through `nativeOnKeyEvent` whenever
  the emergency cursor is off. This targets the game's MOGA-highlighted
  Options, Garage, purchase, and car-selection widgets without restoring the
  alternate global-tab hat path. The Select cursor fallback is also polished:
  analog or held D-pad moves it continuously, A becomes touch-only while it is
  active, and Select is reserved for toggling it rather than leaking a second
  native action. Start remains untouched and Select+Start remains the exit
  chord.
- Run-25 candidate GNU build ID is
  `d8d224a8d53ba6c2b64fd32002c28907a4e9189c`; mapper SHA-256 is
  `7f356d1da344d7ef505100fda03e8e6ef421017dd560b664def1e2675a7bc8ca`.
  Strict armhf and Android syntax builds, the 84-function compatibility audit,
  49-function soft-float audit, and real 2,411-entry OBB test pass. The clean
  PortMaster ZIP SHA-256 is
  `2368bc97ebaf586bc58f463d859f494b5d496f5a5cf75820b6afe585988a385e`;
  the unlimited mapper-test launcher remains unchanged at SHA-256
  `f88511cf564abdb1acc70abbbdca84bc0b289872bbc18c58308e956838ab6831`.
- The Run-25 mapper is installed, flushed, and read-back verified on the card.
  Its SHA-256 and GNU build ID match the candidate. The existing launcher
  remains unlimited, score 20/high graphics, and silent-audio mode; game data
  and saves were not changed.
- Hardware run 25 confirms standard D-pad key delivery fixes most remaining
  menus. Options, Garage, purchase flow, map movement, and top-level shoulder
  navigation are now usable. The only reported issue is the pre-race car
  screen: A starts the race if pressed immediately, but after the rollout
  settles the same A changes the selected car class. The preserved trace is
  `hardware-results/gameplay-run25-standard-dpad-submenus.log`, SHA-256
  `85ddda35fe110d191a411195209e1238cda9b2ddebed33bcbeb30bd91e89e36e`.
  It exited cleanly after 12,404 frames in 240.848 seconds (**51.50 FPS**) with
  code 0.
- Run-25 timing resolves the apparent A inconsistency. A at frame 6,312 opens
  `car_select_new`; a second A at frame 6,362, before rollout completion,
  activates `btn_accept` and starts loading. Once settled, the layout's MOGA
  highlight transfers to `btn_class_dropdown`, so delayed A correctly invokes
  that focused widget. The same layout contains `nav_moga_highlight` and
  `btn_car_action_large`; the next no-reinsert check is D-pad Down followed by
  A after rollout, which should move focus from class selection to the large
  Continue/Buy action.
- The focused Run-25 follow-up is preserved as
  `hardware-results/gameplay-run25-car-class-focus-test.log`, SHA-256
  `fa0d5b17f39ab0aaf09d5a1a0e81c19377e0646b582ffc33f641d4c27f002f01`.
  It ran 8,102 frames in 140.568 seconds (**57.64 FPS**) and exited cleanly
  with code 0. D-pad Down visibly moves the highlight, proving key delivery is
  correct. Selecting the changed class then logs the actual failure:
  `/menus/garage/garage_select_car has no output for CLASS_FILTER_SUPER.
  Ignoring`. The class selector is therefore trapped by a missing title flow
  output, not by the mapper. The safe recovery is B followed by reopening the
  event; Start is the next candidate's direct pre-race action to verify.
- Static inspection of the APK's `org/fmod/FMODAudioDevice` bytecode resolves
  the audio architecture. This FMOD Ex build uses a Java `AudioTrack` pull
  thread, not its compiled-in OpenSL backend. Android calls exported
  `fmodGetInfo` for rate/DSP geometry and `fmodProcess` with a direct
  `ByteBuffer`, then writes signed 16-bit stereo PCM to AudioTrack. Because the
  compatibility launcher enters `nativeOnCreate` directly, that Java object
  and thread never existed; this explains why FMOD appeared initialized but
  every `createSound` returned its internal-error result and no OpenSL calls
  appeared.
- Run 26 replaces only that absent Java pull loop. It resolves the two original
  FMOD JNI exports, supplies the exact JNI direct-buffer slot used by
  `fmodProcess`, queries the original mixer's sample rate and DSP buffer size,
  and queues its PCM unchanged to the proven SDL/ALSA output. FMOD event banks,
  mixing, 3D positioning, music selection, and game logic remain original.
  Audio is enabled in both candidate launchers. The mapper-test build also
  captures 640x480 TGA frames at 6,000/9,000/12,000/15,000/18,000 frames so
  one hardware run can provide the public PortMaster screenshot.
- Run-26 candidate GNU build ID is
  `dcc78e9fbeb11f62c3e2d9754c1114b0e172642f`; mapper SHA-256 is
  `88ae108a1ffb60db4a19bd2188e06f1df41394c2a776626fb126d76e9ca176cd`.
  Strict armhf and Android syntax builds, the 84-function compatibility audit,
  49-function soft-float audit, real 2,411-entry OBB test, JSON/shell checks,
  and clean-package data scan pass. The release-candidate ZIP SHA-256 is
  `70fc466eb4ed0e6ce11b0bee81b8a75f301fa22b058073493a8d4edf46b40035`;
  it contains no APK, OBB, Android `.so`, FMOD bank, or other copyrighted game
  payload. Hardware audio and the Start shortcut are not yet claimed as pass.
- Run 26 is installed, flushed, and read-back verified on the attached card.
  The on-card mapper SHA-256 and GNU build ID match the candidate; launcher
  SHA-256 is
  `0d92b900af723c982599a0faf6bcb871b2c1efd69b5f1891d2ee1569fe374149`.
  It is unlimited, score 20/high graphics, audio enabled, and writes candidate
  captures beside `maptest.log`. Android libraries, OBB, saves, consent, and
  career progress were not modified.
- The current PortMaster submission rules additionally require a 640x480 4:3
  gameplay screenshot, an in-port README/thank-you, and a license, followed by
  documented testing across the major CFW families. Run 26 gathers the missing
  screenshot candidate. The local ZIP is a manually installable v1 release
  candidate; it must not be labeled public v1 until the audio run passes and
  submission metadata is completed.
- Hardware run 26 proves the original FMOD AudioTrack mixer bridge initializes
  correctly: SDL opened 24,000 Hz stereo output, FMOD reported a 1,024-frame,
  five-buffer DSP geometry, and mixer-running stayed at 1. Sound effects were
  audible, but the run was not playable. It completed 3,006 frames in 113.450
  seconds (**26.50 FPS**) and exited cleanly with code 0. The preserved log is
  `hardware-results/gameplay-run26-audio-music-retry-low-fps.log`, SHA-256
  `a7d7253d954aa8d59c599379b42327044447623df63d2c87c31a24de8be638e2`.
- Run 26 records the exact performance regression: in 3,006 frames the title
  made **2,737** `PlayNextTrack` attempts and 2,737 failed MP3 starts because
  `IsUserMusicPlaying()` remained false. This is nearly one filesystem/decoder
  retry per rendered frame and explains both the constantly cycling song list
  and the drop from 57.64 to 26.50 FPS. It is not a graphics-tier regression.
  A 640x480 capture also succeeded and is preserved as
  `hardware-results/run26-capture-06000.tga`, SHA-256
  `62f248c2b28efd8b412e9409827425f58d7367d5b17f7d5597979b809d3a48dd`;
  it shows the car selector and is useful diagnostically but is not the final
  gameplay screenshot.
- Run 27 decouples FMOD output from the external-user-music override. The
  original mixer and SDL/ALSA output stay enabled for working effects, while
  `isAnyMusicPlaying` again returns true to stop the failed soundtrack retry
  loop. This is the smallest controlled FPS fix and retains high graphics and
  all proven input behavior. Music remains intentionally suppressed until its
  separate decoder/file-open failure is solved.
- Run-27 candidate GNU build ID is
  `3f15aed6bf96ece613bbacb570521c6f044316a7`; mapper SHA-256 is
  `36e93203d04a97404a4caa0d1ef07b0850279246467eaa014a91a7c9b7ac2068`.
  Strict armhf and Android syntax builds plus the 84-function compatibility
  and 49-function soft-float audits pass. The clean release-candidate ZIP is
  `266761cd7e93a49deb9049dfd76871ef81bb69546373826bdb98e9021c93e4b4`.
  Hardware FPS with effects enabled is pending.
- Run 27 is installed, flushed, and read-back verified on the attached card.
  The mapper/build ID and launcher match the candidate. The effective test
  combination is score 20/high graphics, unlimited frames, FMOD output on,
  and failed soundtrack retries suppressed. No game data or save state was
  changed.
- Hardware run 27 passes the combined high-graphics, effects, smooth-steering,
  menu, and race-performance gate. The user completed a race with working,
  intelligible sound effects and only minor localized frame drops. The trace
  ended cleanly after 16,277 frames in 333.265 seconds (**48.84 FPS**) with
  exit code 0. It is preserved as
  `hardware-results/gameplay-run27-sfx-performance-pass.log`, SHA-256
  `236859753cb3c52dfe1ffc5d5ae8356ebdd57db41fe40ed8d154bd6a1bd1760b`.
  Only six benign music-manager checks occurred; each immediately observed
  user music as active, so no decoder/file retries were attempted.
- Run 27 also produced the public-package gameplay screenshot candidate at
  native 640x480. `hardware-results/run27-capture-12000.png` shows active
  racing at high visual quality and is preserved with SHA-256
  `f3fbbb26cf52f00f1e83810e471ee000102f376b88a47935fdfc4d216e1bbe93`.
- The remaining pre-race defect is now precisely scoped: ordinary Start does
  not activate the settled car screen, whose focus is trapped on a broken
  class-filter output. Run 28 observes the title's own `car_select_new` layout
  load and, on that screen only, maps Start to a direct press of the visible
  bottom-right Continue/Buy action at 580,425. The touch remains held until
  Start is released. A/B, any replacement layout, and the action itself clear
  the one-bit state so it cannot leak into racing; Select+Start remains the
  clean exit chord and in-race Start remains Pause.
- Run-28 candidate GNU build ID is
  `cd58c27953dd5aa5e4f5729d9cc50b0d1ece05a8`; mapper SHA-256 is
  `27ee7861319a781ad03c7b8f9c9397e4a0c91d9482d573260a435237f7cbe305`.
  Strict armhf and Android builds, the 84-function compatibility audit,
  49-function soft-float audit, native inventory, real 2,411-entry OBB test,
  JSON validation, and launcher syntax checks pass. Hardware verification of
  the direct pre-race action is pending.
- The Run-28 mapper and updated test instructions are installed, flushed, and
  read-back verified on the attached card. The on-card mapper SHA-256 and GNU
  build ID exactly match the candidate. The Run-27 high-graphics/SFX launcher,
  OBB, Android libraries, saves, consent, and career progress were not changed.
- The clean Run-28 release-candidate ZIP now includes the 640x480 gameplay
  screenshot, in-port README/thank-you and controls, and an MIT license for
  the compatibility work. Its SHA-256 is
  `51e4df2370c7c06ac03e2708960bb5354870073ea816c97737621b5aa5e9644e`;
  its contents were scanned and contain no APK, OBB, Android library, FMOD
  bank, music, or other private game payload.
- Hardware run 28 disproves the direct-touch shortcut. The shortcut fired on
  each observed `car_select_new` screen, but its 580,425 press did not activate
  Continue or Buy. Ordinary in-race Start still paused correctly. Career
  progress also exposed `car_select_loadout`, the post-selector modifications
  screen: its visible D-pad highlight could move to Race, yet A still opened a
  modification dropdown and returned to car selection. Only A during rollout
  could advance. The run ended cleanly after 22,115 frames in 482.008 seconds
  (**45.88 FPS**) with exit code 0. Its trace is preserved as
  `hardware-results/gameplay-run28-direct-touch-failed.log`, SHA-256
  `272b4fa33f19200c8b23f1456197f559296d91dae70a47c9053e7f1eb1dbaec8`.
- A fresh audit finds the shared cause in the mapper rather than either title
  layout. Since Run 25, every physical D-pad press has been sent twice: once
  as its genuine MOGA key code 19/20/21/22 and once as a fabricated full-scale
  AXIS_X/AXIS_Y motion. Run-28 frame 1,051 records both `axes=-32767,0` and
  key code 21 for the same press. A real MOGA Pro reports D-pad input only on
  the key channel. The competing UI routes explain how the drawn highlight
  could move while Accept remained attached to the preceding class/mod widget.
- Run 29 removes the failed screen detection/touch shortcut and the legacy
  D-pad-to-stick mirror. D-pad is now delivered only through the title's exact
  native MOGA key path, while the physical analog stick retains its independent
  direct X/Y state and smooth fractional steering. This is a controller-model
  correction, not a screen-specific workaround; it covers both
  `car_select_new` and `car_select_loadout` and leaves normal Start unchanged.
- Run-29 candidate GNU build ID is
  `6e9de3b982ba449f69ac44e5b8804c1e1fbc9bff`; mapper SHA-256 is
  `68c2fccb7fb71238bce4a2c266be30cb223855a3a70c84d31b091982aabbf628`.
  Strict armhf and Android builds, the 84-function compatibility audit,
  49-function soft-float audit, native inventory, real 2,411-entry OBB test,
  JSON validation, and launcher syntax checks pass. One combined hardware run
  must verify settled car Continue/Buy, modifications Race, an ordinary
  submenu, map movement, analog racing, and in-race Pause.
- The clean Run-29 release-candidate ZIP SHA-256 is
  `af9ac9ce98e815b16db4f255475fc0507fbe9ac9c089086241540fff8d8f531a`.
  It retains the Run-27 gameplay screenshot and public metadata and contains no
  private game payload. Automatic card installation was not completed because
  the workspace approval service reached its usage limit; the card still has
  the preserved Run-28 mapper until the two Run-29 files are copied manually.
- Correction: the card was in fact carrying the Run-29 mapper. Its on-card
  SHA-256 is `68c2fccb7fb71238bce4a2c266be30cb223855a3a70c84d31b091982aabbf628`
  and its GNU build ID is `6e9de3b982ba449f69ac44e5b8804c1e1fbc9bff`, both
  exactly matching the Run-29 candidate.
- Hardware run 29 passes the controller-model correction. The single-channel
  D-pad delivers 305 key events with only 18 motion events, and the
  modifications screen is fully usable: the highlight moves, `event_mod` opens
  and closes, and `btn_accept` reaches `event_display_change`. The run ended by
  the exit chord after 16,676 frames in 366.391 seconds (**45.51 FPS**) with
  exit code 0. Its trace is preserved as
  `run30/hardware-results/gameplay-run29-native-moga-dpad.log`, SHA-256
  `0363a7f4f135275cc9dd03d0cde29e8f5b5443000a4f31637bfc441d9935cf71`.
- The same trace scopes the one remaining defect exactly. Across thirteen
  `car_select_new` entries, A produced the advancing `btn_accept` only at 9,
  38 and 47 frames after the layout loaded, and produced the inert
  `btn_generic_accept` at 65, 92, 122, 190, 198, 215 and 254 frames. The
  boundary is the rollout, not any input the mapper sends: one failing entry
  received no D-pad press at all before its A. Every settled A also logs
  `Node /menus/garage/garage_select_car has no output for CLASS_FILTER_SUPER.
  Ignoring`, and `garage_grid_select_car` behaves identically. The Run-25
  focus test shows the same output for every A regardless of how many D-pad
  Up/Down presses precede it, so the D-pad cannot move that highlight at all.
  `car_select_new` therefore parks `nav_moga_highlight` on the class menu and
  offers no D-pad route to `btn_car_action_large`.
- Disassembly of `Java_com_ea_ironmonkey_MogaController_nativeOnMotionEvent`
  identifies the missing input. It stores MOGA `AXIS_X`/`AXIS_Y` at controller
  offsets `0x138`/`0x13c`, then tests `AXIS_Z` (11) and `AXIS_RZ` (14) for
  exact -1.0/+1.0 and converts them into pseudo key codes 190/191 and 188/189.
  `MogaKeyCode` at `0x5449a0` maps 190/191 to menu actions 1054/1055 when the
  controller mode byte at offset `0x132` is zero, and to no action at all when
  it is non-zero. Those two actions are a real MOGA Pro's right stick, are
  reachable through no other button, and have not been sent since Run 25.
  188/189 are the mirror case and are race-only, so they stay unused.
- Run 30 adds exactly that channel and changes nothing else. The R36S reports
  L2/R2 as SDL axes 4 and 5, and the title never reads MOGA trigger axes 17/18,
  so both are free. Each L2/R2 press now emits one discrete `AXIS_Z` tap: a
  full-scale motion event followed immediately by a neutral one, suppressed
  while the touch cursor is active. Because the guest maps 190/191 to nothing
  during a race, this cannot affect gameplay. Run-29 D-pad, analog steering,
  Start and the exit chord are untouched.
- Run 28's `nativeTouchScreenEvent` shortcut is now explained rather than
  merely disproved. `Java_com_ea_ironmonkey_GameGLSurfaceView_nativeTouchScreenEvent`
  resolves its view through a registered-view list and rejects anything else;
  the Run-28 trace logs `AndroidInput: Unregistered view calling
  nativeTouchEvent: 0xaad12070` on every synthetic press. The loader never
  registers a view, so the Select cursor fallback cannot deliver taps either.
  This is recorded for a later run and is not addressed by Run 30.
- Run-30 sources build clean under the project's full strict set
  (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Werror`).
  The 49-function soft-float audit, the 84-function compatibility audit and
  the native inventory all pass against the Run-30 tree. The release build must
  be produced with the Homebrew `arm-unknown-linux-gnueabihf` GCC 15.2.0
  toolchain that built runs 1-29; `run30/build_and_install.sh` performs that
  build, refuses to install a mapper that is not tagged
  `Tag_ABI_VFP_args: VFP registers`, preserves the working Run-29 mapper on the
  card as `nfsmw_mapper.run29`, and verifies the installed SHA-256 by read-back.

- Hardware run 30 disproves the right-stick theory. The mapper logged exactly
  one `G6-MOGA focus-cycle` tap, and the trace shows it fired at frame 5,241,
  after the user had already backed out of `car_select_new` at frame 5,104, so
  the channel was never exercised on the target screen. The run ended cleanly
  after 7,258 frames in 132.986 seconds (**54.58 FPS**) with exit code 0 and is
  preserved as `run31/hardware-results/gameplay-run30-l2r2-focus-cycle-inert.log`,
  SHA-256 `c780e362e99c8b838fffe0b4d485d06c1e6e005d70d5dc77e857f5e335aab289`.
  The installed Run-30 mapper SHA-256 was
  `64e51bd2e6b70bca67a666817d596e8ccf5f80bbd1967dd1ec9523e271e61c15`, GNU build
  ID `0453905668edf1c64fa002ec564c9a94acbf1029`.
- Static analysis then rules the channel out on its own. An immediate-constant
  scan of the car-selection screen class across `0x163000-0x16b000` finds only
  the four D-pad action ids 1002/1003/1004/1005 and no others, so menu actions
  1054/1055 cannot reach that screen at all. Run 31 removes the L2/R2 channel.
- The OBB flow data confirms the class filter is a title defect rather than a
  mapper defect. `published/flow/menus/garage/garage_select_car.sb` declares
  CONTINUE, SKIP_ROLLOUT, FREELOOK, BACK, STORE, SHOWROOM, OPTIONS, RECOMMENDS,
  CARUNLOCKED and the various prompt outputs, and no `CLASS_FILTER_*` output of
  any kind. `CONTINUE` is the one that advances to `garage_select_rollout`, and
  it belongs to `btn_car_action_large`.
- The actual gate is in the screen's own navigation code. Its four D-pad
  handlers at `0x165f08`, `0x166038`, `0x166138` and `0x166274` share one guard
  chain that ends with `ui_state()->byte_0x239 != 0 -> return` before touching
  the highlight index at `this+0x18c`. `ui_state()` is the argument-less
  singleton accessor at `0x7566c`. The setters at `0x3647c4-0x364d70` always
  write byte `0x239` together with the navigation-panel visibility field,
  storing 1 for the touch panel and 0 for the MOGA panel, so it is the title's
  touch-versus-gamepad mode. While it is 1 the car screen accepts no highlight
  movement, which is exactly the behaviour recorded in runs 25, 28, 29 and 30:
  the carousel still swiped on Left/Right, but A always re-fired the same class
  button no matter how many Up/Down presses preceded it.
- Run 31 resolves the singleton through that accessor and holds byte `0x239` at
  0 whenever the emergency cursor is off, because this handheld has no
  touchscreen and gamepad mode is therefore always correct. It logs the
  observed value on every change and logs each override, so one trace settles
  whether this was the blocking guard. No other Run-29 behaviour is changed.
- Run 31 sources compile clean under the project's full strict set. The
  net difference from the Run-29 baseline is the accessor, one state variable,
  one banner line and the override block.

- Run 31 was installed and verified on the card: mapper SHA-256
  `23fb03afe744a1e36d4b2135d1fc093b137312695b0e1ba18c8a4b4c4e5a2dbe`, GNU build
  ID `9b74f1460757566193817b11d56bdb2f8fdc5d6d`.
- Hardware run 31 disproves the pointer-mode guard. The trace contains exactly
  one observation, `G6-UIMODE frame=0 value=0`, and no override line, so
  `ui_state()+0x239` held 0 for the entire session and the car screen was never
  in touch mode. Its trace is preserved as
  `run32/hardware-results/gameplay-run31-pointer-mode-already-zero.log`,
  SHA-256 `83d544eec3496068eea277c29bffbc232b1ace652dc5668813d60d5f3a956b9c`.
- Cross-run correlation now isolates the fault precisely. Every settled A on
  `car_select_new` across runs 29, 30 and 31 - twelve of twelve - produced a
  `CLASS_FILTER_SUPER` or `CLASS_FILTER_GT` output. Two of those presses came
  immediately after a D-pad Right that had already played `swipe_change`, so
  the carousel picture moved while the highlight did not. The highlight is not
  merely hard to move; it is never repositioned at all.
- `showMogaHighlight` for this screen is at `0x167a14`. It selects between
  `mogaHighlightClass` and `mogaHighlightCar` from the carousel index at
  `this+0x18c`, but its first guard is
  `if ((controller->byte_0x133 | controller->byte_0x134) == 0) return;`.
  `setConnected` at `0x7913c` is the only writer of `0x133`, and it opens with
  `if (controller->byte_0x135 == 0) return;`. Byte `0x135` is written once
  during startup at `0x547208` from a JNI-dependent probe that this loader's
  bounded JNIEnv cannot answer truthfully, so the three MOGA state events the
  mapper already sends are discarded and the title never marks a controller
  present for UI purposes.
- The layout data explains why only this screen breaks. `CarSelectionWidget`
  serves both screens from one string block: `car_select_loadout` carries
  `mogaHighlightMod1`, `mogaHighlightMod2`, `mogaHighlightPaint` and
  `mogaHighlightButton`, so its Race action is itself a highlight target and
  D-pad plus A reaches it; `car_select_new` carries only `mogaHighlightCar`
  and `mogaHighlightClass`, with no `mogaHighlightButton`, so
  `btn_car_action_large` is not a MOGA target on that screen at all and the
  only reachable targets are the cars and the class dropdown.
- Run 32 writes `0x133` and `0x135` directly on the controller object, the same
  established technique already used for the analog steering fields at
  `0x138`/`0x13c`, and logs bytes `0x132`-`0x135` on every change. Sources
  compile clean under the project's full strict set. The Run-31 pointer-mode
  override is removed.

- Hardware run 32 fails and regresses. Forcing the controller presence bytes
  did change guest behaviour - `G6-MOGA state frame=0 mode=0 connected=1 alt=0
  available=1`, and the class dropdown became navigable, with D-pad Down now
  producing `CLASS_FILTER_MUSCLE`, `_SPORTS` and `_GT` instead of only
  `_SUPER` - but A still never reached `btn_car_action_large`, and the user
  reports locked cars such as the Tesla becoming selectable. The trace is
  preserved as
  `run32/hardware-results/gameplay-run32-forced-presence-locked-car-regression.log`,
  SHA-256 `dbc3641623d4b99ff2be339d9d91f7511b6a2a237d1f08e29191d7e4e3ebc1dd`.
  Run-32 mapper GNU build ID was `4bc54f14bd0d60c5813770befa93a689b0c3e76b`.
- Run 32 is reverted. The card carries the Run-29 mapper again, read-back
  verified at SHA-256
  `68c2fccb7fb71238bce4a2c266be30cb223855a3a70c84d31b091982aabbf628`, and the
  career save was copied to `files/var/nfstr_save.sb.pre-run32-backup` before
  the revert in case the run-32 session recorded an unearned car.
- Conclusion on the remaining defect. `car_select_new` lists only
  `mogaHighlightCar` and `mogaHighlightClass` as MOGA highlight targets, while
  `car_select_loadout` additionally lists `mogaHighlightButton`. The
  modifications screen therefore exposes its Race action to the highlight ring
  and works; the car-selection screen never exposes `btn_car_action_large` to
  it, so its Continue/Buy action is not reachable by controller in this build.
  `garage_select_car.sb` confirms the flow side: it declares CONTINUE and no
  `CLASS_FILTER_*` output of any kind. This is a limitation of the title's own
  MOGA support for a touch-first screen, not an input the mapper withholds.
- Three input-side theories have now been tested and disproved on hardware:
  the right-stick focus channel (run 30), the pointer-mode guard (run 31) and
  forced controller presence (run 32). The remaining untried mechanism is the
  one the screen was actually designed for. The Select cursor already injects
  taps, but `nativeTouchScreenEvent` discards them: it resolves its view
  through the registered-view walk at `0x54a244`, which compares each
  registered view against the passed object with `IsSameObject`, and the
  run-28 trace logs `AndroidInput: Unregistered view calling nativeTouchEvent`
  on every synthetic press. Making the cursor's taps land would fix this
  screen and any future touch-only screen, and is the only avenue left that
  matches how the screen was built.
- Workaround that is proven to work today: B to leave the car screen, A to
  re-enter, then A again within roughly one second, before the rollout
  settles. It succeeded on every attempt across runs 29-32.
- Public alpha preparation completed on 2026-08-10. The release baseline is
  the reverted Run-29 mapper, not experimental runs 30-32. Public-facing
  documentation now states the car-selection workaround, failed approaches,
  music limitation and nonfunctional cursor accurately. `KNOWN_ISSUES.md` and
  `CONTRIBUTING.md` provide a focused community handoff. Repository ignore
  rules exclude APK/OBB files, extracted Android libraries, card staging,
  binaries, build output and raw TGA captures.
- The reproducible alpha rebuild retains Run 29 mapper SHA-256
  `68c2fccb7fb71238bce4a2c266be30cb223855a3a70c84d31b091982aabbf628`
  and GNU build ID `6e9de3b982ba449f69ac44e5b8804c1e1fbc9bff`. The final
  `v0.1.0-alpha` PortMaster archive passes ZIP integrity checks, contains 11
  expected public files, and has SHA-256
  `3f139a3eb68f00c9b9753a074d1a9bb99578061573553db2ebd5488d6ef70fb1`.
- Public alpha published on 2026-08-10 at
  `https://github.com/Detoy/nfsmw-r36s/releases/tag/v0.1.0-alpha`.
  GitHub independently reports the release asset digest as
  `sha256:3f139a3eb68f00c9b9753a074d1a9bb99578061573553db2ebd5488d6ef70fb1`.
  The source repository is public at `https://github.com/Detoy/nfsmw-r36s`,
  and the remaining car-selection/touch-registration blocker is open as
  help-wanted issue 1 at `https://github.com/Detoy/nfsmw-r36s/issues/1`.

## Gates

| Gate | Status | Exit criterion |
|---|---|---|
| G0 source identity | PASS | APK/OBB and five ARMv7 library hashes pinned |
| G1 static ABI | PASS | dependencies, relocations, exports and JNI entries inventoried |
| G2 map | PASS ON R36S | all five modules mapped and were inspected without guest execution |
| G3 relocate | PASS ON R36S | all 56,142 relocations resolve through ABI-safe providers |
| G4 JNI startup | PASS ON R36S | `JNI_OnLoad` and `nativeOnCreate` return cleanly |
| G5 render | PASS ON R36S | GLES2 context produces repeatable game frames at 640x480 |
| G6 input | RUN 29 SHIPPING / RUNS 30-32 DISPROVED AND REVERTED | car-screen Continue is not a MOGA highlight target in this build; touch injection is the only untried route |
| G7 silent gameplay | PASS: THREE RACES / SMOOTH ANALOG | one race completes with audio intentionally disabled |
| G8 audio | SFX PASS / MUSIC DISABLED | original FMOD mixer produces stable effects; failed soundtrack retries suppressed |
| G9 performance | PASS: 48.84 FPS WITH SFX | high graphics race is playable with only minor localized drops |
| G10 packaging | PUBLIC ALPHA PUBLISHED | collect community reports and review fixes through issue 1 |

## Implementation order

1. Stage one consolidated build containing JNI, OBB, persistent GLES, input,
   silent gameplay, OpenSL audio, and FPS instrumentation.
2. Use its single hardware trace to fix the earliest actual target fault while
   retaining all later gates in the same executable.
3. After one race is verified, finalize the clean PortMaster package and
   preserve the user's APK/OBB as external, non-redistributed game data.

## Do not assume

- Android ARMv7 calls are not ABI-compatible with ArkOS armhf merely because
  both execute ARMv7 instructions. Android uses the base AAPCS for scalar
  floats while armhf libraries use VFP registers.
- Guest Bionic `pthread_*`, `sem_t`, `FILE`, `stat`, and `dirent` objects must
  not be passed directly to glibc.
- A successful relocation is not proof that C++ exceptions can unwind across
  manually mapped modules. Unwind registration needs a dedicated test.
- Audio cannot be treated as two trivial function stubs: the app imports 63
  FMOD C/C++ symbols and expects real object behavior after creation.
