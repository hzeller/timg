[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://github.com/hzeller/timg/blob/main/LICENSE) &nbsp;
[![Ubuntu Build](../../workflows/Ubuntu%20Build/badge.svg)](../../actions?query=workflow%3A"Ubuntu+Build")
[![macOS Build](../../workflows/macOS%20Build/badge.svg)](../../actions?query=workflow%3A"macOS+Build")

timg - Terminal Image and Video Viewer
======================================

A viewer that uses 24-Bit color capabilities and unicode character blocks
to display images in the terminal.

![](./img/sunflower-term.png)

Displays regular images, plays animated gifs, scrolls static images and
plays videos.

Useful if you want to have a quick visual check without leaving the comfort
of your shell and having to start a bulky image viewer. Sometimes this is the
only way if your terminal is connected remotely via ssh. And of course if you
don't need the resolution. While icons typically fit pixel-perfect, larger
images are scaled down to match the resolution.

The command line accepts any number of image/video filenames that it shows
in sequence one per page or in a grid in multiple columns, depending on your
choice of `--grid`. The output is emitted in-line with minimally messing
with your terminal, so you can simply go back in history using your terminals'
scroll-bar (Or redirecting the output to a file allows you to later
simply `cat` that file to your terminal. Even `less -R` seems to be happy with
it).

![Grid view of 4 pictures](./img/grid-timg.png)

### Install

#### Get repo

```bash
git clone https://github.com/hzeller/timg.git
```

#### Get dependencies on Debian/Ubuntu

```bash
sudo apt install cmake git g++ pkg-config
sudo apt install libgraphicsmagick++-dev libturbojpeg-dev libexif-dev libswscale-dev # needed libs

# If you want to include video decoding, also install these additional libraries
sudo apt install libavcodec-dev libavformat-dev

# If you want to recreate the man page
sudo apt install pandoc
```

#### Get dependencies on macOS

```bash
# Homebrew needs to be available to install required dependencies
brew install cmake git GraphicsMagick webp jpeg-turbo libexif # needed libs

# Work around glitch in pkg-config and jpeg-turbo.
brew unlink jpeg && brew link --force jpeg-turbo

# If you want to include video decoding, install these additional libraries
brew install ffmpeg

# If you want to recreate the man page
brew install pandoc
```

#### Compile timg

```bash
cd timg  # Enter the checked out repository directory.
mkdir build  # Generate a dedicated build directory.
cd build
cmake ../ -DWITH_VIDEO_DECODING=On # or Off for no video decoding
make
```

#### Install

You can run timg directly in the build directory using `src/timg`. To install
the binary and [manpage](man/timg.1.md) on your system, type in the build directory:

```bash
sudo make install
```


### Synopsis

```
usage: timg [options] <image/video> [<image/video>...]
Options:
        -g<w>x<h>      : Output pixel geometry. Default from terminal 160x100.
        -C, --center   : Center image horizontally.
        -W, --fit-width: Scale to fit width of available space, even if it exceeds height.
                         (default: scale to fit inside available rectangle)
        --grid=<cols>[x<rows>] : Arrange images in a grid (contact sheet).
        -w<seconds>    : If multiple images given: Wait time between (default: 0.0).
        -a             : Switch off anti aliasing (default: on)
        -b<str>        : Background color to use on transparent images (default '').
        -B<str>        : Checkerboard pattern color to use on transparent images (default '').
        --auto-crop[=<pre-crop>] : Crop away all same-color pixels around image.
                         The optional pre-crop is the width of border to
                         remove beforehand to get rid of an uneven border.
        --rotate=<exif|off> : Rotate according to included exif orientation or off. Default: exif.
        -U             : Toggle Upscale. If an image is smaller (e.g. an icon) than the
                         available frame, enlarge it to fit.
        -V             : Only use Video subsystem. Don't attempt to probe image decoding first.
                         (useful, if you stream video from stdin).
        -I             : Only  use Image subsystem. Don't attempt video decoding.
        -F, --title    : Print filename as title above each image.
        -f<filelist>   : Read newline-separated list of image files to show. Can be there multiple times.
        -o<outfile>    : Write to <outfile> instead of stdout.
        -E             : Don't hide the cursor while showing images.
        --threads=<n>  : Run image decoding in parallel with n threads
                         (Default 2, half #cores on this machine)
        --version      : Print version and exit.
        -h, --help     : Print this help and exit.

  Scrolling
        --scroll=[<ms>]       : Scroll horizontally (optionally: delay ms (60)).
        --delta-move=<dx:dy>  : delta x and delta y when scrolling (default: 1:0).

  For Animations, Scrolling, or Video
  These options influence how long/often and what is shown.
        --loops=<num> : Number of runs through a full cycle. Use -1 to mean 'forever'.
                        If not set, videos loop once, animated images forever
                        unless there is more than one file to show (then: just once)
        --frames=<num>: Only show first num frames (if looping, loop only these)
        -t<seconds>   : Stop after this time, no matter what --loops or --frames say.
```

### Examples
```bash
timg some-image.jpg                # display a static image
timg -g50x50 some-image.jpg        # display image fitting in box of 50x50 pixel

# Multiple images
timg *.jpg                         # display all *.jpg images
timg --title *.jpg                 # .. show name in title (short option -F)
timg --grid=3x2 *.jpg              # arrange in 3 columns, 2 rows in terminal
timg --fit-width --grid=3 *.jpg    # maximize use of column width (short: -W)
timg --grid=3 -t5 *.gif            # Load gifs one by one in grid. Play each for 5sec.

# Putting it all together; making an alias to list images; let's call it ils = 'image ls'
# This prints images two per row with a filename title. Only showing one frame
# so for animated gifs only the first frame is shown statically.
alias ils='timg --grid=2 --center --title --frame=1 '

# ... using this alias
ils *.jpg *.gif

# Show a PDF document, use full width of terminal, trim away empty border
timg -W --auto-crop some-document.pdf
timg --frames=1 some-document.pdf    # Show a PDF, but only first page

# Open an image from a URL. URLs are internally actually handled by the
# video subsystem, so it is treated as a single-frame 'film', nevertheless,
# many image-URLs just work. But some image-specific features, such as trimming
# or scrolling, won't work.
timg -C https://i.kym-cdn.com/photos/images/newsfeed/000/406/282/2b8.jpg

# Sometimes, it is necessary to manually crop a few pixels from an
# uneven border before the auto-crop finds uniform color all-around to remove.
# For example with --auto-crop=7 we'd remove first seven pixels around an image,
# then do the regular auto-cropping.
#
# The following example loads an image from a URL; --auto-crop does not work with
# that, so we have to get the content manually, e.g. with wget. Piping to
# stdin works; in the following example the stdin input is designated with the
# special filename '-'.
#
# For the following image, we need to remove 3 pixels all around before
# auto-crop can take over removing the remaining whitespace successfully:
wget -qO- https://imgs.xkcd.com/comics/a_better_idea.png | timg --auto-crop=3 -

timg multi-resolution.ico   # See all the bitmaps in multi-resolution icons-file
timg --frames=1 multi-resolution.ico  # See only the first bitmap in that file

timg some-video.mp4         # Watch a video.

# If you read a video from a pipe, it is necessary to skip attempting the
# image decode first as this will consume bytes from the pipe. Use -V option.
youtube-dl -q -o- -f'[height<480]' 'https://youtu.be/dQw4w9WgXcQ' | timg -V -

# Show animated gif, possibly limited by timeout, loops or frame-count
timg some-animated.gif      # show an animated gif forever (stop with Ctrl-C)
timg -t5 some-animated.gif                   # show animated gif for 5 seconds
timg --loops=3 some-animated.gif             # Loop animated gif 3 times
timg --frames=3 --loops=1 some-animated.gif  # Show only first three frames
timg --frames=1 some-animated.gif            # Show only first frame. Static image.

# Scroll
timg --scroll some-image.jpg       # scroll a static image as banner (stop with Ctrl-C)
timg --scroll=100 some-image.jpg   # scroll with 100ms delay

# Create a text with 'convert' and send to timg to scroll
convert -size 1000x60 xc:none -box black -fill red -gravity center \
      -pointsize 42 -draw 'text 0,0 "Watchen the blinkenlights."' -trim png:- \
      | timg --scroll=20 -

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
echo some-image.jpg | fzf --preview='timg -E --frames=1 --loops=1 -g $(( $COLUMNS / 2 - 4 ))x$(( $FZF_PREVIEW_LINES * 2 )) {}'

# Also, you could store the output and cat later to your terminal...
timg -g80x40 some-image.jpg > /tmp/imageout.txt
cat /tmp/imageout.txt

# Of course, you can redirect the output to somewhere else. I am not suggesting
# that you rickroll some terminal by redirecting timg's output to a /dev/pts/*
# you have access to, but you certainly could...

# Of course, you can go really crazy by storing a cycle of an animation. Use xz
# for compression as it seems to deal with this kind of stuff really well:
timg -g60x30 --loops=10 nyan.gif | xz > /tmp/nyan.term.xz

# ..now, replay the generated ANSI codes on the terminal. Since it would
# rush through as fast as possible, we have to use a trick to wait between
# frames: Each frame has a 'move cursor up' escape sequence that contains
# an upper-case 'A'. We can latch on that to generate a delay between frames:
xzcat /tmp/nyan.term.xz | gawk '/A/ { system("sleep 0.1"); } { print $0 }'

# You can wrap all that in a loop to get an infinite repeat.
while : ; do xzcat... ; done

# (If you Ctrl-C that loop, you might need to use 'reset' for terminal sanity)
```

### Terminal considerations

Note, this requires that your terminal can display
[24 bit true color][24-bit-term] and is able to display the unicode
characters [▄](U+2584 - 'Lower Half Block') and [▀](U+2580 - 'Upper Half Block').
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
