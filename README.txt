ForceFullRes dylib (source)
===========================

What this does
--------------
This source creates a dynamic library that, when injected into Angry Birds Go (1.9.0),
rebinds `glViewport` and `glScissor` to force the viewport to the device's native pixel
resolution (portrait/landscape aware). This commonly removes black bars caused by
internal aspect clamping.

Files included
--------------
- ForceFullRes.m    : main Objective-C source
- fishhook.h/.c     : compact fishhook implementation
- README            : this file

Build (Theos)
--------------
1. Place files in a Theos tweak project or compile as a dynamic library.
2. Example Theos Makefile snippet (not included here) should compile with:
   THEOS_DEVICE_IP = <your device>
   include $(THEOS)/makefiles/common.mk
   TWEAK_NAME = ForceFullRes
   ForceFullRes_FILES = ForceFullRes.m fishhook.c
   ForceFullRes_LIBRARIES = c objc
   include $(THEOS_MAKE_PATH)/tweak.mk

Build (clang) - for testing on macOS/iOS SDK
-------------------------------------------
clang -dynamiclib -o ForceFullRes.dylib ForceFullRes.m fishhook.c -framework UIKit -framework OpenGLES -fobjc-arc

Injection
---------
- On jailbroken device: use tweak injection (Substrate/Substitute/Etc) or insert_dylib/optool.
- On non-jailbroken device (sideloaded): use insert_dylib to add the dylib to the binary's load commands, then codesign.

Caveats
-------
- Some versions of the game use Metal instead of OpenGL. This dylib targets OpenGL ES (glViewport/glScissor).
- If the app uses Metal, a different hook (MTLViewport or CAMetalLayer) is required.
- Test carefully and keep backups.