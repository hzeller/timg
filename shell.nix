# This is a nix-shell for use with the nix package manager.
# If you have nix installed, you may simply run `nix-shell`
# in this repo, and have all dependencies ready in the new shell.

{pkgs ? import (builtins.fetchTarball {
  # Descriptive name to make the store path easier to identify
  name = "nixos-2021-05";
  # Commit hash
  url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/21.05.tar.gz";
  # Hash obtained using `nix-prefetch-url --unpack <url>`
  sha256 = "1ckzhh24mgz6jd1xhfgx0i9mijk6xjqxwsshnvq789xsavrmsc36";
}) {}}:


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
    ];
}
