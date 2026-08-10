# NFS Most Wanted 1.3.128 ARMv7 ABI contract

This report is tied to the exact APK and OBB hashes in the project README.
Changing either input requires a new audit.

## Native modules

| Module | SHA-256 | Unique imports | Relocations |
|---|---|---:|---:|
| `libNimble.so` | `ace73446e2e526a5dd1f098cb4d50dae5f18a83706856d91337c588ff5e6d4b6` | 10 | 38 |
| `libapp.so` | `a73dcc170552866af52bf2d4f5822606307fb42aed1ef0df2e469fdfa374ff43` | 540 | 49,048 |
| `libc++_shared.so` | `ee86a2cb55d2d205260793c67997cdd9cf3d2e7588ecf7c122e4790b7c2c2005` | 106 | 4,209 |
| `libfmodevent.so` | `cfb17d2de3d95732a364c29ee032a501eabe26b9fc7af47433f5537fa85c6ce0` | 209 | 1,201 |
| `libfmodex.so` | `7c29ab877deb862c0154b548a057d35eb70cd0cf20b714054b65921e6961a6e8` | 117 | 1,646 |

Relocation types are limited to `R_ARM_RELATIVE`, `R_ARM_ABS32`,
`R_ARM_GLOB_DAT`, and `R_ARM_JUMP_SLOT`. `libapp.so` alone has 45,699 relative
relocations, so relocation correctness and cache/protection handling matter.

## Dependency graph

```text
libapp.so
├── libc++_shared.so
├── libfmodex.so
├── libfmodevent.so
│   └── libfmodex.so
├── libNimble.so
├── libGLESv2.so
├── libEGL.so
├── liblog.so
├── libjnigraphics.so
├── libc.so
├── libm.so
└── libdl.so
```

The FMOD libraries and Nimble also declare the obsolete Android
`libstdc++.so`. This is the small NDK system C++ support library, not GNU's
full host `libstdc++`; its symbols must be audited and supplied deliberately.

## `libapp.so` import profile

Important subsets of the 540 unique imports:

| Family | Count | Provider policy |
|---|---:|---|
| GLES entry points | 145 | host GLES2, with softfp thunks where scalar floats cross the ABI |
| FMOD C/C++ entry points | 63 | bundled FMOD modules |
| libc++/C++ ABI symbols | 50 | bundled `libc++_shared.so` |
| pthread calls | 41 | Bionic-to-glibc object bridge |
| ARM EABI/unwind | 19 | compiler runtime plus guest-aware unwind plan |
| semaphore calls | 7 | Bionic-to-glibc side table |
| Android bitmap | 3 | runtime shim |
| Android logging | 3 | stderr/log-file shim |
| EGL | 1 | controlled `eglGetProcAddress` wrapper |

The provider counts overlap in a few cases because the FMOD event module
re-exports core methods. Resolver order must follow `DT_NEEDED` and pin each
C++ family to the matching guest provider instead of using unrestricted host
`dlsym`.

## ARM procedure-call convention

No module declares `Tag_ABI_VFP_args`. This is Android's softfp/base AAPCS.
ArkOS armhf host libraries pass scalar floating-point arguments in VFP
registers. Directly resolving functions such as `glClearColor`,
`glDepthRangef`, `glLineWidth`, `glPolygonOffset`, `glSampleCoverage`,
`glTexParameterf`, scalar `glUniform*f`, and scalar `glVertexAttrib*f` to host
GLES will corrupt their arguments.

Compile entry thunks in an armhf executable with
`__attribute__((pcs("aapcs")))`; let each thunk make a normal hard-float call
to the host library. Pointer-to-float APIs such as `glUniform4fv` do not pass
the float values through registers and can normally bind directly.

The same rule applies to host math functions with float/double parameters or
returns. They require softfp wrappers even though symbol names match.

## Bionic object and data boundaries

Do not bind these families directly to glibc:

- `pthread_mutex_t`, `pthread_cond_t`, `pthread_rwlock_t`, `pthread_once_t`,
  pthread attributes and thread startup
- `sem_t` and semaphore waits
- `FILE`, `__sF`, stdio functions and `fwide`
- `stat`, `statfs`, `dirent`, `readdir` and `readdir_r`
- `sigaction`, jump buffers and time-related structures where layouts differ
- `__errno`, `_ctype_`, `_tolower_tab_`, stack guard and Android symbol
  versions

Use side tables keyed by guest addresses for opaque synchronization objects.
Use synthetic Bionic streams and translate them in every stdio wrapper.

## JNI lifecycle and input surface

Exported lifecycle/render entry points include:

```text
Java_com_ea_ironmonkey_GameActivityMain_nativeOnCreate
Java_com_ea_ironmonkey_GameActivityMain_nativeSurfaceCreated
Java_com_ea_ironmonkey_GameActivityMain_nativeSurfaceChanged
Java_com_ea_ironmonkey_GameActivityMain_nativeOnResume
Java_com_ea_ironmonkey_GameActivityMain_nativeOnPause
Java_com_ea_ironmonkey_RunLoop_nativeOnRunLoopTick
```

Relevant signatures from `classes.dex`:

```text
nativeOnCreate()V
nativeSurfaceCreated(GL10, EGLConfig)V
nativeSurfaceChanged(GL10, int width, int height)V
nativeRestoreContext()Z
nativeOnRunLoopTick()V
nativeTouchScreenEvent(int, int, float, float)V
nativeTouchPadEvent(int, int, float, float)V
nativeOnPhysicalKeyDown(int, int)V
nativeOnPhysicalKeyUp(int, int)V
nativeOnKeyEvent(com.bda.controller.KeyEvent)V
nativeOnMotionEvent(com.bda.controller.MotionEvent)V
nativeOnStateEvent(com.bda.controller.StateEvent)V
```

The fake Java side must at minimum supply activity/run-loop methods,
filesystem paths, OBB identity/path, locale/device information, bitmap access,
MOGA event accessors, and Nimble class lookup. Startup tracing on the R36S
should grow this contract method-by-method; unknown essential methods should
fail loudly rather than return a universal zero.

## Audio decision

`libapp.so` imports 63 FMOD symbols and all are present in the bundled FMOD
pair. The primary route is therefore:

1. map `libfmodex.so` and `libfmodevent.so` with their Bionic imports bridged;
2. intercept their `dlopen("libOpenSLES.so")` / `dlsym` sequence;
3. provide `slCreateEngine` and stable values for `SL_IID_ENGINE`,
   `SL_IID_PLAY`, `SL_IID_RECORD`, `SL_IID_ANDROIDSIMPLEBUFFERQUEUE`, and
   `SL_IID_ANDROIDCONFIGURATION`;
4. translate the small OpenSL object/interface subset FMOD uses to a Linux
   output queue, preferably SDL2 audio for minimal deployment dependencies.

Before that bridge exists, the runtime should deliberately make FMOD init fail
cleanly and prove render/input/gameplay. Returning fake success from
`FMOD_EventSystem_Create` without valid objects is unsafe.
