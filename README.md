Minesweeper
===========

Minesweeper clone by yakov and 8.5tails.

There is a "luck" feature: Click the four-leaf clover. If you enable luck mode (green clover), whenever you left-click
on a hidden cell, the program will attempt to rearrange the mines so that there's no mine in that cell, if it's possible
to do so without modifying the visible numbers. This way you can avoid losing due to having to guess in the
endgame. Analogously, there's a bad luck mode (red clover), which is useful for training yourself to avoid bad
habits. If the clover is blue, you'll receive good luck when you click next to a visible number, but not when you click
anywhere else.

Note that chording (middle-click) currently ignores luck.


Screenshot
----------

![screenshot 1](screenshots/screenshot_1.png)


Building
--------

**Windows**:

Either run cmake-gui, or "open folder" in Visual Studio. The required libraries are included in the source tree, so it
should just work.


**Linux**:

You must have installed SDL2, SDL2_image, and lpsolve-5.5, which is used for the luck feature.

Configure (example):

    $ cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++

(Optionally) Change config options:

    $ ccmake build

Press 'c' to configure and then 'g' to generate.

Build:

    $ cmake --build build -v
