# BlanKart

BlanKart is a major modification of an in development version of SRB2Kart v2 to make it closer to SRB2Kart gameplay while adding new features, and techincal improvements.

If you're interested in helping out, theres a matrix room located [here](https://matrix.to/#/#blankart:matrix.org)!

# Notice

This is still in active development and things are going to change. If you find any bugs besure to report to the [issue tracker](https://codeberg.org/NepDisk/blankart/issues)!

## Techincal Features
- Entire refactorings of internal systems such as the items, the menus, the demos and many more for easier maintainability.
- Full Support for UDMF, Terrain, Bots, ACS, Waypoints and many other technical features backported from Ring Racers. Also includes some extensions to these features such as bot drifting, new terrain effects and map header toggles.
- Ring Racers' software renderer and its multithreading as a base for the software renderer with many additions ported from SRB2Kart Saturn and SRB2Classic.
- Improved OpenGL Renderer based on some changes from SRB2Kart Saturn and various other SRB2 builds with support for features such as palette rendering, support for portals and improved geometry pre-processing.
- Native support for both SRB2Kart and Ring Racers custom skins and followers.
- Backwards compatiblity with most SRB2Kart maps, assets* and scripts\*.
- A map patching system to allow custom map things to be patched onto maps. Useful for adding rings, waypoints and other objects to existing maps.
- Support for many SRB2Kart v1 custom client extensions from Saturn and other kart clients such as vote themes.
- Better Lua and SOC exposure for things such as adding custom boosts, custom boost chains, custom items+item odds, custom menus, and many more things.
- More Lua hooks for ease of scripting.
- Most features from SRB2Kart Saturn.
- New gameplay features that can be toggled such as Rings, New/Alternative Items, 4 tier drifting, stacking+chaining and many more!
- Many cvars to allow server hosts to tune their own personal settings just right!
- Proper dedicated server build for easier deployment in server hosting enviroments.

*Art assets are remapped from the SRB2Kart v1 palette to the SRB2 2.2 palette automatically based on addon file name. This also affects scripts as well making them run as they would in SRB2Kart v1. Having the K prefix or the .kart extension triggers this. You can also do addfile -c to enable it or addfile -n to disable it.

## Dependencies
- curl (Linux/OS X only)
- zlib (Linux/OS X only)
- libpng (Linux/OS X only)
- libminiupnpc (Linux/OS X only)
- libgme (Client only, Linux/OS X only)
- libopenmpt (Client only, Linux/OS X only)
- SDL3 (Client only, Linux/OS X only)
- libsndfile (Client only, Linux/OS X only)
- OpenAL-Soft (Client only, Linux/OS X only)

## Compiling
MSVC is unsupported, so please use GCC, Clang or Zig as your compiler.

### Client
Linux:
```
git clone --recursive https://codeberg.org/NepDisk/blankart.git
cd blankart
mkdir build
cd build
cmake ..
make -j$(nproc)
```

Windows MSYS2:
```
git clone --recursive https://codeberg.org/NepDisk/blankart.git
cd blankart
mkdir build
cd build
cmake -G "MSYS Makefiles" .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5
make -j$(nproc)
```

### Dedicated Server
Linux:
```
git clone --recursive https://codeberg.org/NepDisk/blankart.git
cd blankart
mkdir build
cd build
cmake .. -DSRB2_CONFIG_DEDICATED=ON
make -j$(nproc)
```

Windows MSYS2:
```
git clone --recursive https://codeberg.org/NepDisk/blankart.git
cd blankart
mkdir build
cd build
cmake -G "MSYS Makefiles" .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DSRB2_CONFIG_DEDICATED=ON
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
