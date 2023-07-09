#!/usr/bin/env bash
#
# Building an appimage. Here fairly 'manual' to better understand what
# is needed.
#
# Takes cmake parameters
##

set -e

mkdir -p app-build AppDir

(
    cd app-build
    cmake "$@" -DCMAKE_INSTALL_PREFIX=../AppDir/usr ../
    make install
)

# We need an icon and the AppRun coming from
cp img/sunflower-term.png AppDir/timg.png
install $(command -v AppRun) AppDir/

# Get all shared libs we depend on
mkdir -p AppDir/usr/lib
install $(ldd AppDir/usr/bin/timg | awk '{print $3}') AppDir/usr/lib

cat > AppDir/timg.desktop <<EOF
[Desktop Entry]
Terminal=true
Name=timg
Exec=timg
Icon=timg
Type=Application
Categories=Utility;
EOF
desktop-file-validate AppDir/timg.desktop

appimagetool AppDir/
