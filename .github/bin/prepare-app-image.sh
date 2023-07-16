#!/usr/bin/env bash
#
# FYI:
#   - on nixos: nix-shell -p appimagekit pkgs.glibc.static
#   - on ubuntu: sudo apt install desktop-file-utils
#     and e.g. get  https://github.com/AppImage/appimagetool/releases/
##

set -e
set -x

APPIMAGETOOL=${APPIMAGETOOL:-appimagetool}

rm -rf app-build AppDir
mkdir -p app-build AppDir

(
    cd app-build
    cmake "$@" ../
    make
    strip src/timg
    install -D src/timg ../AppDir/usr/bin/timg
)

# We need an icon and the AppRun coming from
install -D img/sunflower-term.png AppDir/timg.png

mkdir -p AppDir/usr/lib
install $(ldd AppDir/usr/bin/timg | awk '{print $3}') AppDir/usr/lib

# There are also a few other libraries that the runtime apparently needs.
# TODO: fix that by statically linking these ?
for f in libpthread libmount libblkid libmvec libexpat libdl librt libcap ; do
    install /lib/x86_64-linux-gnu/${f}.* AppDir/usr/lib
done

(
    # Get the AppRun directly from the source and then compile statically
    # for minimum surprises at runtime.
    rm -f /tmp/AppRun.c
    wget -O/tmp/AppRun.c https://raw.githubusercontent.com/AppImage/AppImageKit/master/src/AppRun.c
    gcc -static /tmp/AppRun.c -o AppDir/AppRun
)

cat > AppDir/timg.desktop <<EOF
[Desktop Entry]
Terminal=true
Name=timg
Exec=timg
Icon=timg
Type=Application
Categories=Utility;
EOF

"${APPIMAGETOOL}" AppDir/
du -h timg*AppImage

# Rough litmus test to verify it starts up at least
LD_LIBRARY_PATH="" PATH="" AppDir/AppRun --version
