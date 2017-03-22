timg - Terminal Image Viewer
============================

A viewer that uses 24-Bit color capabilities and unicode character blocks
to display images in the terminal.

![](./img/sunflower-term.png)

Displays regular images, plays animated gifs or allows to scroll static images.

Very useful for if you want to have a quick visual check without starting a
bulky image viewer ... and don't care about resolution.

### Install

```bash
git clone https://github.com/hzeller/timg.git
cd timg/src
sudo apt-get install libwebp-dev libgraphicsmagick++-dev    # required libs.
make
sudo make install
```

### Synopsis

```
usage: timg [options] <image> [<image>...]
Options:
        -g<w>x<h>  : Output pixel geometry. Default from terminal 144x88
        -s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).
        -d<dx:dy>  : delta x and delta y when scrolling (default: 1:0).
        -w<seconds>: If multiple images given: Wait time between (default: 0.0).
        -t<seconds>: Only animation or scrolling: stop after this time.
        -c<num>    : Only Animation or scrolling: number of runs through a full cycle.
        -C         : Clear screen before showing image.
        -F         : Print filename before showing picture.
        -v         : Print version and exit.
If both -c and -t are given, whatever comes first stops.
If both -w and -t are given for some animation/scroll, -t takes precedence
```

### Examples
```bash
timg some-image.jpg         # display a static image
timg -g50x50 some-image.jpg # display image fitting in box of 50x50 pixel

timg *.jpg                  # display all *.jpg images

# Show animated gif with timeout.
timg some-animated.gif      # show an animated gif (stop with Ctrl-C)
timg -t5 some-animated.gif  # show animated gif for 5 seconds

# Scroll
timg -s some-image.jpg      # scroll a static image as banner (stop with Ctrl-C)
timg -s100 some-image.jpg   # scroll with 100ms delay

# Scroll direction. Horizontally, vertically; how about diagonally ?
timg -s -d1:0 some-image.jpg  # scroll with dx=1 and dy=0, so horizontally.
timg -s -d-1:0 some-image.jpg # scroll horizontally in reverse direction.
timg -s -d0:2 some-image.jpg  # vertical, two pixels per step.
timg -s -d1:1 some-image.jpg  # diagonal, dx=1, dy=1

# Also, you could store the output and cat later to your terminal...
timg -g80x40 some-image.jpg > /tmp/imageout.txt
cat /tmp/imageout.txt

# Of course, you can go really crazy by storing a cycle of an animation. Use xz
# for compression as it seems to deal with this kind of stuff really well:
timg -g60x30 -C -c1 nyan.gif | xz > /tmp/nyan.term.xz
# ..now, replay that cycle in a loop. Latch on the frame marker with awk to delay
while : ; do xzcat /tmp/nyan.term.xz | gawk '/\[[0-9]+A/ { system("sleep 0.1"); } { print $0 }' ; done
# (If you ctrl-c that loop, you might need to use 'reset' for terminal sanity)
```

Note, this requires that your terminal can display
[24 bit true color][24-bit-term] and is able to display the unicode
character ▀ (U+2580, 'Upper Half Block').
If not, it doesn't show anything or it is garbage.

Tested terminals: `konsole` >= 2.14.1, `gnome-terminal` > 3.6.2 look good,
recent xterms also seem to work (albeit with less color richness).
Like gnome-terminal, libvte based terminals in general should work, such as
Xfte or termite.
Also QTerminal is confirmed working.

Linux console seems to be limited in colors and does not show the block
character - if you know how to enable the unicode character or full color
there, please let me know.

For Mac users, the iTerm2 >= 3.x should work, please confirm if you have this
setup.

[24-bit-term]: https://gist.github.com/XVilka/8346728
