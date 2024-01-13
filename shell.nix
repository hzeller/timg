# This is a nix-shell for use with the nix package manager.
# If you have nix installed, you may simply run `nix-shell`
# in this repo, and have all dependencies ready in the new shell.

{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = with pkgs;
    [
       stdenv
       cmake
       git   # for TIMG_VERSION_FROM_GIT
       pkg-config
       graphicsmagick
       libjpeg
       libdeflate
       ffmpeg
       libexif
       libsixel
       librsvg cairo
       poppler

       # Don't include qoi and stb by default to see if the cmake
       # fallback to third_party/ works.
       #qoi
       #stb

       openslide
       pandoc
       clang-tools_13  # clang-format
    ];
}
