#!/usr/bin/env bash
#
##

set -e
set -x

mkdir -p app-build AppDir

(
    cd app-build
    cmake "$@" -DCMAKE_INSTALL_PREFIX=../AppDir/usr ../
    make install
)

# We need an icon and the AppRun coming from
install -D img/logo.svg AppDir/usr/share/icons/timg.svg

cat > AppDir/timg.desktop <<EOF
[Desktop Entry]
Terminal=true
Name=timg
Exec=timg
Icon=timg
Type=Application
Categories=Utility;
EOF
