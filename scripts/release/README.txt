Amped2-Recomp @VERSION@
=======================

A native PC port of Amped 2 (original Xbox, 2003), made by statically recompiling
the game's Xbox code to C. No emulator. Early, experimental release.

Project page and source: https://github.com/bertibertig/Amped2-Recomp
License: GPL-3.0-or-later (LICENSE.txt); third-party licenses in licenses/.

Unofficial fan project, not affiliated with or endorsed by Microsoft.


1. Prepare the game files
-------------------------

This package contains NO game data. You need a legally obtained Amped 2 disc for
the original Xbox, PAL/EU version (title ID 4D530041; default.xbe SHA-1
38e63cd016bf084d999e0a911933a52191bfc378). Other versions (NTSC-U, NTSC-J) are
not tested and most likely do not work.

a) Make a disc image (.iso) of your disc, e.g. on a modded Xbox or with an
   Xbox-compatible DVD drive. A full disc image (Redump style) and a
   game-partition XISO both work.

b) Extract the image into the game_files folder next to this file, with
   extract-xiso (https://github.com/XboxDev/extract-xiso) or
   xdvdfs (https://github.com/antangelo/xdvdfs):

    extract-xiso -x "Amped 2.iso" -d game_files
    xdvdfs unpack "Amped 2.iso" game_files

   Already have an extracted disc folder? Copy its contents into game_files.

c) Check the layout: default.xbe must sit directly in game_files, not in a
   subfolder (not game_files/Amped 2/default.xbe). The disc is about 4 GB;
   keep every file and folder as extracted (upper/lower case does not matter).

    Amped2-Recomp-.../
      amped2_recomp       (amped2_recomp.exe on Windows)
      run.sh              (run.bat and run.ps1 on Windows)
      README.txt          (this file)
      game_files/
        default.xbe
        Audio/
        Levels/
        Movies/
        ...               (the rest of the disc)

Without game_files/default.xbe, ./run.sh (run.bat, run.ps1) stops with a message
saying where the files belong. Saves go to ~/.local/share/xboxrecomp (Windows:
%LOCALAPPDATA%\xboxrecomp), not into game_files.


2. Start the game
-----------------

Linux (x86-64, glibc 2.39 or newer: Ubuntu 24.04+, Fedora 40+, Arch, SteamOS 3.6+):

    ./run.sh

  Needs SDL2 and a Vulkan driver, e.g.
    Ubuntu/Debian: sudo apt install libsdl2-2.0-0 libvulkan1 mesa-vulkan-drivers
    Arch:          sudo pacman -S sdl2 vulkan-icd-loader  (+ your GPU's Vulkan driver)
    Fedora:        sudo dnf install SDL2 vulkan-loader mesa-vulkan-drivers

  The game opens a resizable window (internal resolution 640x480, 4:3).
  Saves go to ~/.local/share/xboxrecomp.

  Graphics settings:  ./run.sh --settings
  opens a settings screen in the terminal before the game starts: internal
  resolution (1x-5x), aspect ratio (4:3, 16:9, 16:10, 21:9), frame rate
  (original 60 fps or unlocked up to your monitor's refresh rate) and window
  mode (windowed, fullscreen, borderless, which monitor). Arrow keys move and
  change, Enter saves and starts, Q quits. The choices are remembered in
  ~/.config/amped2-recomp/settings.ini. In the game Alt+Enter or F11 switches
  between windowed and fullscreen.

Windows (10 or 11, x86-64): double-click run.bat, or right-click run.ps1 and
  choose "Run with PowerShell" (in a PowerShell console:
    powershell -ExecutionPolicy Bypass -File .\run.ps1 ).
  Both write the console output to run.log next to them; run.ps1 also shows
  its last lines when the game ends with an error.
  Windows is experimental and NOT playable yet:
  - It only has the software (CPU) renderer: about 4-6 fps in the menus and
    about 2 fps on the slope. The fast Vulkan renderer and the graphics
    settings are Linux-only so far.
  - So far it has only been tested on Linux with Wine and Proton, not on a
    real Windows PC. Reports from Windows 10/11 are welcome.
  - Under Wine/Proton the movies stay black, and there may be no music
    (sound effects work).
  - run.ps1 is new and has not been tried on a real Windows PC yet; if it
    does not start, use run.bat.

There is no macOS package yet.


3. Controls
-----------

In the game: left stick = turn / spin / flip / butter, A = accelerate (release to
jump), B = jib / rail slide, Y = lip trick, right stick = grabs, Back = reset
boarder, Start = pause. In the menus A or Start selects, B goes back.

Any XInput-style controller works (Xbox 360/One/Series, 8BitDo in X-input mode,
most SDL-supported pads). Pads can be plugged in at any time. LB/RB =
White/Black, triggers are analog, rumble works.

Keyboard (player 1, only while the game window has the focus):
  W A S D = left stick (turn, spin, flip)     I J K L = right stick (grabs)
  Space or Z = A (accelerate / jump, select)  O or X = B (jib, back)
  U or V = Y (lip trick)                      C = X
  Enter or Escape = Start (pause)             Backspace or Tab = Back (reset)
  Arrows = D-pad      1 / 3 = left / right trigger      Q / E = White / Black
  Shift / Ctrl = left / right stick click


4. Options (environment variables)
----------------------------------

  AMPED2_SKIP_LOGOS=0     show the Microsoft Game Studios logo (skipped by default)
  AMPED2_SKIP_INTRO=1     skip the intro movie too
  AMPED2_RENDERER=cpu     software renderer (reference, slow; Linux only, Windows
                          always uses it)
  AMPED2_PAD_TRACE=1      log every controller button change

  AMPED2_RESOLUTION_SCALE=1..5, AMPED2_ASPECT_RATIO=4:3|16:9|16:10|21:9,
  AMPED2_FRAMERATE, AMPED2_FRAME_LIMIT, AMPED2_WINDOW_MODE, AMPED2_DISPLAY
                          override the --settings choices for one run (Linux)

e.g.  AMPED2_SKIP_INTRO=1 ./run.sh
On Windows: set AMPED2_SKIP_INTRO=1 in a console, then start run.bat from it;
in PowerShell:  $env:AMPED2_SKIP_INTRO = '1'; .\run.ps1


5. Known issues
---------------

- Early release: only the menus and the first rides of the career (Millicent 1)
  have been checked; a full career, saving and loading over several sessions and
  multiplayer are not verified yet.
- Rides in the original 60 fps mode run at about 55 fps instead of a steady 60.
- The rider has no shadow yet.
- Not tested on a Steam Deck yet.
- Audio is mixed to stereo (no 5.1); the game's reverb and effects are mixed
  in dry.
- The software renderer (Windows, AMPED2_RENDERER=cpu) draws stray triangles
  stretched out from trees and other objects.
- Windows: see section 2 (software renderer only, very slow, no graphics
  settings, --settings does not work; black movies and possibly no music
  under Wine/Proton).

Bug reports: https://github.com/bertibertig/Amped2-Recomp/issues
Please attach the console output (./run.sh 2>&1 | tee run.log; Windows: run.log).
