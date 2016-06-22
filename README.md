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
        -g<w>x<h>  : Output pixel geometry. Default from terminal 223x114
        -s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).
        -t<timeout>: Animation or scrolling: only display for this number of seconds.
        -c<num>    : Animation or scrolling: number of runs through a full cycle.
        -C         : Clear screen first before display
        -F         : Print filename before showing picture.
        -v         : Print version and exit.
If both -c and -t are given, whatever comes first stops.
```

### Examples
```bash
timg some-image.jpg         # display a static image

timg *.jpg                  # display all *.jpg images

timg some-animated.gif      # show an animated gif (stop with Ctrl-C)
timg -t5 some-animated.gif  # show animated gif for 5 seconds

timg -s some-image.jpg      # scroll a static image as banner (stop with Ctrl-C)
timg -s100 some-image.jpg   # scroll with 100ms delay right to left
timg -s-100 some-image.jpg  # negative scroll delay scrolls left to right

# Also, you could store the output and cat later to your terminal...
timg some-image.jpg > /tmp/imageout.txt
cat /tmp/imageout.txt

# Of course, you can go really crazy by storing a cycle of an animation. Use xz
# for compression as it seems to deal with this kind of stuff really well:
timg nyan.gif -g60x30 -c1 | xz > /tmp/nyan.term.xz
# ..now, replay that cycle in a loop. Latch on the frame marker with awk to delay
while : ; do xzcat /tmp/nyan.term.xz | gawk '/\[[0-9]+A/ { system("sleep 0.1"); } { print $0 }' ; done
# (If you ctrl-c that loop, you might need to use 'reset' for terminal sanity)
```

Note, this requires that your terminal can display 24 bit colors and is able
to display the character ▀ (U+2580, 'Upper Half Block'). If not, it doesn't
show anything or it is garbage.

Tested terminals: `konsole` >= 2.14.1, `gnome-terminal` >= 3.6.2 look good,
recent xterms also seem to work (albeit with less color richness).

Linux console seems to be limited in colors and does not show the block
character - if you know how to enable the unicode character or full color
there, please let me know.

For Mac users, the iTerm2 >= 3.x should work, please confirm if you have this
setup.
