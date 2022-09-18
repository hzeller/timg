# This is a nix-shell for use with the nix package manager.
# If you have nix installed, you may simply run `nix-shell`
# in this repo, and have all dependencies ready in the new shell.

{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = with pkgs;
    [
       stdenv
       cmake
       pkg-config
       graphicsmagick
       libjpeg
       zlib
       ffmpeg
       libexif
       openslide
       pandoc
       clang-tools_13  # clang-format
    ];
}
