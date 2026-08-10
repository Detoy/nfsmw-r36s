Need for Speed: Most Wanted (2012 Android) compatibility port
=============================================================

Thank you to the PortMaster project, the R36S and Linux handheld community,
and everyone who tested the ARMv7, GLES2, controller, and audio compatibility
work on real hardware.

This package contains no Electronic Arts game data. You must supply the
supported legally obtained version 1.3.128 APK and OBB; see gamedata/README.txt
for setup instructions.

Controls
--------
Left stick: steering and map movement
D-pad: native MOGA menu navigation
A: accept/drift
B: back
L1: brake/reverse
R1: nitrous
L1/R1 in menus: change top-level section
Start: pause and map selection
Select+Start: exit to PortMaster

Sound effects are supported. The soundtrack is disabled in this release
because the original Android MP3 path repeatedly fails on Linux and cuts the
frame rate roughly in half.

PUBLIC ALPHA LIMITATION
-----------------------
On the pre-race car-selection/purchase screen, A works only during the short
rollout window. If it stops working, press B to leave, A to re-enter, then A
again immediately. The later modifications screen works normally with D-pad
and A. Select toggles an experimental cursor, but its taps are not functional.
