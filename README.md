# SRB2Kart

[SRB2Kart](https://srb2.org/mods/) is a kart racing mod based on the 3D Sonic the Hedgehog fangame [Sonic Robo Blast 2](https://srb2.org/), based on a modified version of [Doom Legacy](http://doomlegacy.sourceforge.net/).

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

## Disclaimer
Kart Krew is in no way affiliated with SEGA or Sonic Team. We do not claim ownership of any of SEGA's intellectual property used in SRB2.
