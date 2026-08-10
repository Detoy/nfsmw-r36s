# Contributing

Thanks for helping with the R36S compatibility port.

## Legal boundary

Never commit, attach, or link to the game APK, OBB, extracted Android shared
libraries, assets, audio, saves, or decompiled copyrighted code. Contributions
should contain only independently written compatibility code, build scripts,
documentation, hashes, and diagnostic logs that do not embed game payloads.

## Start here

1. Read [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and
   [PORTING_STATUS.md](PORTING_STATUS.md).
2. Use the pinned APK/OBB hashes documented in [README.md](README.md), obtained
   from your own lawful copy.
3. Build the runtime on an armhf host or with the documented hard-float cross
   toolchain.
4. Run the compatibility, soft-float, native-import and OBB-index checks.
5. Test on real R36S hardware and preserve a minimal log.

The public alpha source in `runtime/` is the stable hardware-tested baseline.
Failed controller experiments are summarized in the status document and issue
tracker so contributors do not need to repeat them.

## Reports and patches

Open an issue before undertaking a large rewrite. For a controller fix, state
which native input path changes and why it should make
`btn_car_action_large` reachable without regressing the working modifications
screen, race controls, locked-car state, or pause behavior.

Keep pull requests narrow and include:

- root cause and expected behavior;
- files and native offsets affected;
- local validation commands and results;
- hardware log, FPS, and exit code;
- confirmation that the release archive contains no proprietary payload.
