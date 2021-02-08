timg(1) - A terminal image and video viewer
============================================

## SYNOPSIS

  `timg` [<options>] <image/video> [<image/video>...]

## DESCRIPTION

Show images, play animated gifs, scroll static images or play videos in the
terminal. Even show PDFs if needed.

Useful if you want to have a quick visual check without starting a
bulky image viewer and don't need the resolution.

The command line accepts any number of image/video filenames that it shows
in sequence.

The special filename "`-`" stands for standard input, so you can read
an image from a pipe. If the input from a pipe is a video, use the `-V` option
(see below).

Under the hood, timg uses GraphicsMagick to open and decode a wide
range of image formats. It also uses libav to decode and play videos or images
from files and URLs.

## OPTIONS

### General Options
  * `-g` <width>x<height>:
     Output image to fit inside given geometry. By default, the size is
     determined by the available space in the terminal.
     The image is scaled to fit inside the available box to fill the
     screen; see `-W` if you want to fill the width.

  * `-C`:
    Center image horizontally.

  * `-W`:
    Scale to fit width of the terminal.

  * `--grid`=<cols>[x<rows>]:
    Arrange images in a grid. If only one parameter is given, arranges in a
    square grid (e.g. --grid=3 makes a 3x3 grid). Alternatively, you can choose
    columns and rows (e.g. --grid=3x2).

  * `-w` <seconds>:
    Wait time between images when multiple images are given on the command
    line. Fractional values are allowed, so `-w 3.1415` would wait approximately
    π seconds between images.

  * `-a`:
    Switch off antialiasing. The images are scaled down to show on the
    minimal amount of pixels, so some smoothing is applied for best visual
    effect. This switches off that smoothing.

  * `-b` <background-color>:
    Set the background color for transparent images. Common HTML/SVG color
    strings are supported, such as `red`, `rgb(0, 255, 0)` or `#0000ff`.

  * `-B` <checkerboard-other-color>:
    Show the background of a transparent image in a checkerboard pattern with
    the given color, which alternates with the `-b` color.
    Like in `-b`, HTML/SVG color strings are supported.

  * `--autocrop`[=<pre-crop>]:
    Trim same-color pixels around the border of image before displaying. Use
    this if there is a boring even-colored space aorund the image which uses
    too many of our available few pixels.

    The optional pre-crop is number of pixels to unconditionally trim
    all around the original image, for instance to remove a thin border. The
    link in the [EXAMPLES](#EXAMPLES) section shows an example how this
    improves showing an xkcd comic with a border.

  * `--rotate=<exif|off>`:
      If 'exif', rotate the image according to the exif data stored
      in the image. With 'off', no rotation is extracted or applied.

  * `-U`:
    Toggle Upscale. If an image is smaller than the terminal size, scale
    it up to fit the terminal.

    By default, larger images are only scaled down and images smaller than the
    available pixels in the terminal are left at the original size (this
    helps assess small deliberately pixeled images such as icons in their
    intended appearance). This option scales up smaller images to fit available
    space.

  * `-V`:
    This is a video, directly read the content as video and don't attempt to
    probe image decoding first.

    Usually, timg will first attempt to interpret the data as image, but
    if it that fails, will fall-back to try interpret the file as video.
    However, if the file is coming from stdin, the first bytes used to probe
    for the image have already been consumed so the fall-back would fail in
    that case... Arguably, this should be dealt with automatically but isn't :)

    Long story short: if you read a video from a pipe, use -V.
    See link in [EXAMPLES](#EXAMPLES) section for a an example.

  * `-I`:
    This is an image, don't attempt to fall back to video decoding. Somewhat
    the opposite of `-V`.

  * `-F`:
    Print filename before each image.

  * `-E`:
    Don't hide the cursor while showing images.

  * `-v`, `--version`:
    Print version and exit.

  * `-h`, `--help`:
    Print command line option help and exit.

### Scrolling

  * `--scroll`[=<ms>]:
    Scroll horizontally with an optional delay between updates (default: 60ms)

  * `--delta-move`=<dx>\:<dy>:
    Scroll with delta x and delta y. The default of 1:0 scrolls it horizontally,
    but with this option you can scroll vertically or even diagonally.

### For Animations, Scrolling, or Video

Usually, animations are shown in full in an infinite loop. These options
limit infinity.

  * `-t` <seconds>:
    Stop an animation after these number of seconds.
    Fractional values are allowed.

  * `--loops=`<num>:
    Number of loops through a fully cycle of an animation or video.
    A value of -1 stands for 'forever'.

    If this option is not set, videos behave like --loops=1 (show exactly once)
    and animated gifs like --loop=-1 (loop forever).

  * `--frames=`<frame-count>:
    Only render the first `frame-count` frames in an animation or video.
    If frame-count is set to 1, the output behaves like a static image.

## RETURN VALUES

Exit code is

  * `0`:
    On reading and displaying all images successfully.

  * `1`:
    If any of the images could not be read or decoded or if there was no
    image provided.

  * `2`:
    If an invalid option or parameter was provided.

  * `3`:
    If timg could not determine the size of terminal (not a tty?). Provide
    `-g` option to provide size of the output to be generated.


## ENVIRONMENT

  * `TIMG_USE_UPPER_BLOCK`:
     If this environment variable is set to the value `1`, timg will use the
     U+2580 - 'Upper Half Block' (▀) unicode character.

    To display pixels, timg uses a unicode half block and sets the foreground
    color and background color to get two vertical pixels. By default, it uses
    the U+2584 - 'Lower Half Block' (▄) character to achieve this goal. This
    has been chosen as it resulted in the best image in all tested terminals
    (konsole, gnome terminal and cool-retro-term). So usually, there is no
    need to change that. But if the terminal or font result in a funny output,
    this might be worth a try. This is an environment variable because if it
    turns out to yield a better result on your system, you can set it once
    in your profile and forget about it.

## EXAMPLES

Some example invocations including scrolling diagonally or streaming an
online video are collected at <https://github.com/hzeller/timg#examples>

## KNOWN ISSUES

This requires a terminal that can deal with unicode characters and 24 bit
color escape codes. This will be problematic on really old installations or
if you want to display images on some limited text console.

The option `-V` should not be necessary; timg should internally buffer bytes
it uses for probing.

## BUGS

Report bugs to <http://github.com/hzeller/timg/issues>

## COPYRIGHT

Copyright (c) 2016..2021 Henner Zeller. This program is free software,
provided under the GNU GPL version 2.0 or later
<https://gnu.org/licenses/gpl.html>.

## SEE ALSO

GraphicsMagick, ffmpeg(1)
