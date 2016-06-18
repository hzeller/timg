timg - Terminal Image Viewer
============================

An image viewer that uses the 24-Bit color capabilities and character blocks
as pixels to display images in a terminal.

![](./img/sunflower-term.png)

Displays regular images, shows animated gifs or allows to scroll static images.

Very useful for if you want to have a quick visual check without starting a
bulky image viewer ... and don't care about resolution.

### Install

```bash
git clone https://github.com/hzeller/timg.git
cd timg/src
sudo apt-get install libgraphicsmagick++-dev     # required library.
make
sudo make install
```

### Synopsis

```
usage: timg [options] <image>
Options:
        -g<w>x<h>  : Output pixel geometry. Default from terminal 132x88
        -s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).
        -t<timeout>: Animation or scrolling: only display for this number of seconds.
```

### Examples
```
timg some-image.jpg         # display a static image

timg some-animated.gif      # show an animated gif (stop with Ctrl-C)
timg -t5 some-animated.gif  # show animated gif for 5 seconds

timg -s some-image.jpg      # scroll a static image as banner (stop with Ctrl-C)

# Also, you could store the output and cat later to your terminal...
timg some-image.jpg > /tmp/imageout.txt
cat /tmp/imageout.txt

```

Note, this requires that your terminal can display 24 bit colors and is able
to display the character ▀ (U+2580, 'Upper Half Block'). If not, it doesn't
show anything or it is garbage.

Tested terminals: `konsole` >= 2.14.1, `gnome-terminal` >= 3.6.2 look good,
recent xterms also seem to work (albeit with less color richness).
Linux console seems to be limited in colors and does not show the block
character - if you know how to enable the unicode character or full color
there, please let me know.

