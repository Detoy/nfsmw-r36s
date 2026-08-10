# ARM32 runtime

The runtime has passed gates G2 and G3 on the R36S. Its combined test
repeats strict mapping, inventories target-side symbol providers, checks the
independent SDL/KMSDRM GLES, controller and SDL/ALSA audio paths, then applies
ARM `REL` relocations using mapped guest libraries, soft-float thunks, host
providers and explicit Bionic/Android compatibility bridges.

Setting `NFSMW_RUN_CONSTRUCTORS=1` additionally calls `DT_INIT` and
`DT_INIT_ARRAY` functions in dependency order with an enter/leave trace around
each call. After a clean return it runs registered C++ destructors and ELF
finalizers in reverse order. Setting `NFSMW_RUN_JNI=1` additionally calls
`JNI_OnLoad` and `nativeOnCreate` through the bounded fake JavaVM/JNIEnv;
unknown JNI calls fail loudly with their guest return address. OBB access is
still disabled.

Build on an armhf Linux host (or in an armhf cross-build environment):

```sh
make
build/nfsmw_mapper /path/to/gamefiles/android-libs
```

Expected successful combined output ends with:

```text
COMBINED PASS: mapping, provider census, host preflights and relocation phase A completed
```

`make syntax-android` is a source-only check for macOS machines with the
Android NDK already installed. Its output is an Android binary if linked and
must not be shipped as the R36S runtime.

The next stage grows the fake JavaVM/JNIEnv from target startup traces. Do not
extend this mapper by copying the
section-header-based loader from the CTW experiment: Android release binaries
may omit section headers, and runtime linking data belongs in `PT_DYNAMIC`.
