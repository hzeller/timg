timg - Terminal Image and Video Viewer
======================================

A viewer that uses 24-Bit color capabilities and unicode character blocks
to display images in the terminal.

![](./img/sunflower-term.png)

Displays regular images, plays animated gifs, scrolls static images and
plays videos.

Very useful for if you want to have a quick visual check without starting a
bulky image viewer ... and don't care about resolution.

### Install

```bash
git clone https://github.com/hzeller/timg.git
cd timg/src
sudo apt-get install libwebp-dev libgraphicsmagick++-dev    # required libs.

# If you want to include video decoding
sudo apt-get install pkg-config libavcodec-dev libavformat-dev libswscale-dev

make WITH_VIDEO_DECODING=1
sudo make install
```

### Synopsis

```
usage: timg [options] <image/video> [<image/video>...]
Options:
        -g<w>x<h>  : Output pixel geometry. Default from terminal 203x126
        -w<seconds>: If multiple images given: Wait time between (default: 0.0).
        -a         : Switch off antialiasing (default: on)
        -W         : Scale to fit width of terminal (default: fit terminal width and height)
        -U         : Toggle Upscale. If an image is smaller than
                     the terminal size, scale it up to full size.
        -V         : This is a video, don't attempt to probe image deocding first
                     (useful, if you stream from stdin).
        -b<str>    : Background color to use on transparent images (default '').
        -B<str>    : Checkerboard pattern color to use on transparent images (default '').
        -C         : Clear screen before showing images.
        -F         : Print filename before showing images.
        -E         : Don't hide the cursor while showing images.
        -v         : Print version and exit.

  Scrolling
        -s[<ms>]   : Scroll horizontally (optionally: delay ms (60)).
        -d<dx:dy>  : delta x and delta y when scrolling (default: 1:0).

  For Animations and Scrolling
        -t<seconds>: Stop after this time.
        -c<num>    : Number of runs through a full cycle.
        -f<num>    : Only animation: number of frames to render.

If both -c and -t are given, whatever comes first stops.
If both -w and -t are given for some animation/scroll, -t takes precedence
```

### Examples
```bash
timg some-image.jpg         # display a static image
timg -g50x50 some-image.jpg # display image fitting in box of 50x50 pixel

timg *.jpg                  # display all *.jpg images

timg multi-resolution.ico   # See all the bitmaps in multi-resolution icons-file

timg -W some-document.pdf   # Show a PDF document, use full width of terminal

timg some-video.mp4         # Watch a video.

# If you read a video from a pipe, it is necessary to skip attempting the
# image decode first as this will consume bytes from the pipe. Use -V
youtube-dl -q -o- -f'[height<480]' 'https://www.youtube.com/watch?v=dQw4w9WgXcQ' | ./timg -V -

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

# Background color for transparent images (SVG-compatible strings are supported)
timg -b 'red' some-transparent-image.png
timg -b 'rgb(0, 255, 0)' some-transparent-image.png
timg -b '#0000ff' some-transparent-image.png

# Checkerboard/Photoshop-like background on transparent images
timg -b white -B gray some-transparent-image.png

# Another use: can run use this in a fzf preview window:
echo some-image.jpg | fzf --preview='timg -E -f1 -c1 -g $(( $COLUMNS / 2 - 4 ))x$(( $FZF_PREVIEW_HEIGHT * 2 )) {}'

# Also, you could store the output and cat later to your terminal...
timg -g80x40 some-image.jpg > /tmp/imageout.txt
cat /tmp/imageout.txt

# Of course, you can go really crazy by storing a cycle of an animation. Use xz
# for compression as it seems to deal with this kind of stuff really well:
timg -g60x30 -c10 nyan.gif | xz > /tmp/nyan.term.xz

# ..now, replay the generated ANSI codes on the terminal. Since it would
# rush through as fast as possible, we have to use a trick to wait between
# frames: Each frame has a 'move cursor up' escape sequence that contains
# an upper-case 'A'. We can latch on that to generate a delay between frames:
xzcat /tmp/nyan.term.xz | gawk '/A/ { system("sleep 0.1"); } { print $0 }'

# You can wrap all that in a loop to get an infinite repeat.
while : ; do xzcat... ; done

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
