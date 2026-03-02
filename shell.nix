with import <nixpkgs> {};
stdenv.mkDerivation {
  name = "blankart-env";
  buildInputs = [ cmake gcc ninja sdl3 zlib libpng openal libsndfile miniupnpc curl libopenmpt game-music-emu ];
  # cmake flags, add -DSRB2_CONFIG_NOVERIFYIWADS=ON to the below command to build without IWAD verification
  # Usage from build/: cmake .. $cmakeFlags
  cmakeFlags = ["-DSDL3_INCLUDE_DIR=${lib.getDev sdl3}/include -DOPENMPT_INCLUDE_DIR=${lib.getDev libopenmpt}/include"];
}
