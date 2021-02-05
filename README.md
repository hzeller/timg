timg - Terminal Image and Video Viewer
======================================

A viewer that uses 24-Bit color capabilities and unicode character blocks
to display images in the terminal.

![](./img/sunflower-term.png)

Displays regular images, plays animated gifs, scrolls static images and
plays videos.

Very useful if you want to have a quick visual check without starting a
bulky image viewer ... and don't care about resolution.

### Install

#### First get the repository and change into the `src` directory

```bash
git clone https://github.com/hzeller/timg.git
cd timg/src
```

#### Compiling on Debian/Ubuntu
```bash
sudo apt-get install libwebp-dev libgraphicsmagick++-dev    # required libs.

# If you do not want to include video decoding
make WITH_VIDEO_DECODING=0

# If you want to include video decoding, install these additional libraries
sudo apt-get install pkg-config libavcodec-dev libavformat-dev libswscale-dev
make
```

#### Compiling on macOS
```bash
# Homebrew needs to be available to install required dependencies
brew install GraphicsMagick webp  # required libs 

# If you do not want to include video decoding
LDFLAGS=-L${HOMEBREW_PREFIX}/lib make WITH_VIDEO_DECODING=0

# If you want to include video decoding, install these additional libraries
brew install ffmpeg
LDFLAGS=-L${HOMEBREW_PREFIX}/lib make
```


#### Install the binary and the [manpage](man/timg.1.md)
```bash
sudo make install
```


### Synopsis

```
usage: timg [options] <image/video> [<image/video>...]
Options:
        -g<w>x<h> : Output pixel geometry. Default from terminal 204x114
        -C        : Center image horizontally.
        -W        : Scale to fit width of terminal (default: fit terminal width and height)
        -w<seconds>: If multiple images given: Wait time between (default: 0.0).
        -a        : Switch off antialiasing (default: on)
        -b<str>   : Background color to use on transparent images (default '').
        -B<str>   : Checkerboard pattern color to use on transparent images (default '').
        --trim[=<pre-crop>]
                  : Trim: auto-crop away all same-color pixels around image.
                    The optional pre-crop is pixels to remove beforehand
                    to get rid of an uneven border.
        -U        : Toggle Upscale. If an image is smaller than
                    the terminal size, scale it up to full size.
        -V        : This is a video, don't attempt to probe image deocding first.
                    (useful, if you stream from stdin).
        -I        : This is an image. Don't attempt video decoding.
        -F        : Print filename before showing images.
        -E        : Don't hide the cursor while showing images.
        -v, --version : Print version and exit.
        -h, --help    : Print this help and exit.

  Scrolling
        --scroll=[<ms>]         : Scroll horizontally (optionally: delay ms (60)).
        --delta-move=<dx:dy>  : delta x and delta y when scrolling (default: 1:0).

  For Animations and Scrolling
  These are usually shown in in full in an infinite loop. These options influence that.
        -t<seconds>: Stop after this time.
        -c<num>    : Number of runs through a full cycle.
        -f<num>    : For animations: only render first num frames.

If both -c and -t are given, whatever comes first stops.
If both -w and -t are given for some animation/scroll, -t takes precedence
```

### Examples
```bash
timg some-image.jpg         # display a static image
timg -g50x50 some-image.jpg # display image fitting in box of 50x50 pixel

timg *.jpg                  # display all *.jpg images

# Show a PDF document, use full width of terminal, trim away empty border
timg -W --trim some-document.pdf

# Open an image from a URL. URLs are internally actually handled by the
# video subsystem, so it is treated as a single-frame 'film', nevertheless,
# many image-URLs just work. But some image-specific features, such as trimming
# or scrolling, won't work.
timg -C https://i.kym-cdn.com/photos/images/newsfeed/000/406/282/2b8.jpg

# Sometimes, it is necessary to manually crop a few pixels from an
# uneven border before the auto-trim finds uniform color all-around to remove.
# For example with --trim=7 we'd remove first seven pixels around an image,
# then do the regular auto-cropping.
#
# The following example loads an image from a URL; --trim does not work with
# that, so we have to get the content manually, e.g. with wget. Piping to
# stdin works.
#
# For this image, we need to remove 3 pixels all around before
# auto-trim can take over successfully:
wget -qO- https://imgs.xkcd.com/comics/a_better_idea.png | timg --trim=3 -

timg multi-resolution.ico   # See all the bitmaps in multi-resolution icons-file

timg some-video.mp4         # Watch a video.

# If you read a video from a pipe, it is necessary to skip attempting the
# image decode first as this will consume bytes from the pipe. Use -V option.
youtube-dl -q -o- -f'[height<480]' 'https://youtu.be/dQw4w9WgXcQ' | timg -V -

# Show animated gif with timeout.
timg some-animated.gif      # show an animated gif (stop with Ctrl-C)
timg -t5 some-animated.gif  # show animated gif for 5 seconds

# Scroll
timg --scroll some-image.jpg # scroll a static image as banner (stop with Ctrl-C)
timg --scroll=100 some-image.jpg   # scroll with 100ms delay

# Scroll direction. Horizontally, vertically; how about diagonally ?
timg --scroll --delta-move=1:0 some-image.jpg  # scroll with dx=1 and dy=0, so horizontally.
timg --scroll --delta-move=-1:0 some-image.jpg # scroll horizontally in reverse direction.
timg --scroll --delta-move=0:2 some-image.jpg  # vertical, two pixels per step.
timg --scroll --delta-move=1:1 some-image.jpg  # diagonal, dx=1, dy=1

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

# Of course, you can redirect the output to somewhere else. I am not suggesting
# that you rickroll some terminal by redirecting timg's output to a /dev/pts/*
# you have access to, but you certainly could...

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
characters[▄] (U+2584 - 'Lower Half Block') and [▀] (U+2580 - 'Upper Half Block').
If not, it doesn't show anything or it looks like gibberish. These days, most
terminals have these minimum requirements.

By default, `timg` uses the 'lower half block' to show the pixels. Depending
on the font the terminal is using, using the upper block might look better,
so it is possible to change the default with an environment variable.
Play around with this value if the output looks poor on your terminal. I found
that on my system there is no difference for `konsole` or `xterm` but the
[`cool-retro-term`][cool-retro-term] looks better with the lower block, this is why it is the
default. To change, set this environment variable:

```
export TIMG_USE_UPPER_BLOCK=1   # change default to use upper block.
```

Tested terminals: `konsole` >= 2.14.1, `gnome-terminal` > 3.6.2 look good,
recent xterms also seem to work (albeit with less color richness).
Like gnome-terminal, libvte based terminals in general should work, such as
Xfte or termite.
Also QTerminal is confirmed working.

Linux console seems to be limited in colors and does not show the block
character - if you know how to enable the unicode character or full color
there, please let me know.

For Mac users, at least the combination of macOS 11.2 and iTerm2 3.4.3 works. 

[24-bit-term]: https://gist.github.com/XVilka/8346728
[cool-retro-term]: https://github.com/Swordfish90/cool-retro-term
