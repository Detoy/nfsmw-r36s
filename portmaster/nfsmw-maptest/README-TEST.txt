NFS Most Wanted R36S consolidated gameplay test
===============================================

This build runs all completed gates in one launch: mapping, relocation,
constructors, JNI startup, compressed OBB access, persistent GLES2 surface,
controller dispatch, game-loop rendering, and the original FMOD mixer through
an AudioTrack-to-SDL compatibility pump. It records every completed gate
before moving to the next one.

The legally owned OBB is already staged beside the runtime. Run
"NFS Most Wanted - Mapper Test" from Ports. Use Select+Start together to leave
the interactive loop cleanly; there is no automatic time limit. Rendering uses
the proven high visual tier and sound effects are enabled. Music is temporarily
suppressed because its failed decoder retry loop halves rendering speed. Select toggles the yellow
touchscreen fallback cursor, the D-pad or left stick moves it, and A taps.
Select hides it again. Outside cursor mode, the D-pad is delivered only through
the original MOGA key path; the left analog stick remains an independent axis.

The mapper automatically saves candidate release screenshots at roughly 100,
150, 200, 250, and 300 seconds. Please spend part of that interval in a race;
the images will be collected from the card with the log.

Most menus use A to activate the highlighted item and B to go back. This run
specifically verifies the native MOGA focus path. On the settled pre-race car
screen, use D-pad Down to move from Class to Continue/Buy, then A. On the
modifications screen, move the highlight to Race with the D-pad, wait briefly,
then press A. Do not use the quick-A rollout shortcut. Also verify map movement,
one ordinary submenu, analog steering, and in-race Start/Pause in the same run.
The result is written to:

  ports/nfsmw-maptest/maptest.log

Copy that log back to the project after the card is returned to the computer.
