# BlanKart

BlanKart is a modification of SRB2Kart v2 Indev to make it closer to SRB2Kart gameplay while adding new features, and techincal improvements.

# Notice

This is still in active development and things are going to change. If you find any bugs besure to report to  the [issue tracker](https://codeberg.org/NepDisk/blankart/issues)!

## Techincal Features
- Full Support for UDMF, Terrain, Bots, ACS, New Waypoints and many other technical features backported from Ring Racers. Also includes some extensions to these features such as bot drifting, new terrain effects and map header toggles.
- Ring Racers' software renderer and its multithreading.
- Backwards compatiblity with most SRB2Kart maps, assets* and scripts\*.
- A map patching system to allow custom map things to be patched onto maps. Useful for adding rings, waypoints and other objects to existing maps.
- Support for many SRB2Kart v1 custom client extensions from Saturn and other clients such as vote themes.
- A hardcoded Lua library for adding custom boosts and boost chains to the game.

*Art assets are remapped from the SRB2Kart v1 palette to the SRB2 2.2 palette automatically based on addon file name. This also affects scripts as well making them run as they would in SRB2Kart v1. Having the K prefix or the .kart extension triggers this. You can also do addfile -c to enable it or addfile -n to disable it.

## Optional Features
- Colorized player hud
- Minimap Angle display
- Many features ported from Saturn

## Optional Gameplay Features
- Rings (Based on how they were in v2 Indev)
- New items (Such as the Flame Shield and the Bubble Shield)
- 4 tier drifts (Blue, Red, Purple, Rainbow)
- Boost Stacking
- Boost Chaining
- Slipdashing

## Dependencies
- SDL2 (Linux/OS X only)
- SDL2-Mixer (Linux/OS X only)
- libupnp (Linux/OS X only)
- libgme (Linux/OS X only)
- libopenmpt (Linux/OS X only)

## Compiling

Linux:
```
git clone https://codeberg.org/NepDisk/blankart.git
cd blankart
mkdir build
cd build
cmake ..
make -j$(nproc)
```

Windows MSYS2:
```
git clone https://codeberg.org/NepDisk/blankart.git
cd blankart
mkdir build
cd build
cmake -G "MSYS Makefiles" ..
./_deps/sdl2_mixer-src/external/download.sh
cmake -G "MSYS Makefiles" ..
make -j$(nproc)
```

## How to install
- Obtain an executable
- Grab the assets from the [blankart-assets repo](https://codeberg.org/NepDisk/blankart-assets) 
- Compile the pk3s by running buildall.bat on windows or buildall.sh on *nix.
- Copy the generated pk3s from the folder `out/` and your compiled executable into a folder with the [SRB2Kart 1.6 assets](https://github.com/STJr/Kart-Public/releases/download/v1.6/AssetsLinuxOnly.zip) (such as an existing install).

# Disclaimer
BlanKart is in no way affiliated with Kart Krew Dev or Sonic Team Jr. This is just a project meant for fun. If you have any issues report to us not them.

We also do not claim ownership of any of SEGA's intellectual property used in BlanKart.
