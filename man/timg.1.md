% timg(1)
% Henner Zeller
% Dec 2023

# NAME

timg - A terminal image and video viewer

# SYNOPSIS

  **timg** [&lt;*options*&gt;] &lt;*image/video*&gt; [&lt;*image/video*&gt;...]

# DESCRIPTION

Show images, play animated gifs, scroll static images or play videos in the
terminal. Even show PDFs.

View images without leaving the comfort of your shell.
Sometimes this is the only way if your terminal is connected remotely via ssh.

The command line accepts any number of image/video filenames (or read a list
of filenames from a file) and shows these in sequence one per page or
in a grid in multiple columns, depending on your choice of **-\-grid**.
The output is emitted in-line with minimally messing
with your terminal, so you can simply go back in history using your terminals'
scroll-bar (Or redirecting the output to a file allows you to later
simply **cat** that file to your terminal. Even **less -R** seems to be happy
with such output).

The special filename "-" stands for standard input, so you can read
an image from a pipe. If the input from a pipe is a video, use the **-V** option
(see below).

Under the hood, `timg` uses various image libraries to open and
decode a wide range of image formats. It uses threads to open and decode images
in parallel for super-fast viewing experience for many images.
To play videos, it uses libav from files and URLs.
With **-I** or **-V** you can choose to use only one of these file decoders
({GraphicsMagick, turbojpeg, qoi} or libav respectively).

# OPTIONS

## General Options

Most likely commonly needed options first.

**-p** *&lt;[h|q|s|k|i]&gt;*, **-\-pixelation**=*[h|q|s|k|i]*
:    Choice for pixelation of the content.

     Available values

     **half** (short 'h')
     : Uses unicode half block characters, this is the lowest resolution.
     : Color is using a lower or upper half block and chooses the foreground
     : color and background color to make up two vertical pixels per character
     : cell.
     : Half blocks have a pixel aspect ratio of about 1:1 and represent colors
     : correctly, but they look more 'blocky'.

     **quarter** (short 'q')
     : This chooses a Unicdoe character with small sub-blocks for four pixels
     : per characcter cell.
     : Quarter blocks will have a pixel aspect ratio of 1:2 (timg will stretch
     : the picture accordingly, no worries), and can only
     : represent colors approximately, as the four quadrant sub-pixels can only
     : be foreground or background color. This increases the spatial resolution
     : in x-direction at expense of slight less color accuracy.
     : It makes it look less 'blocky' and usually better.

     **sixel** (short 's')
     : Sixel output allows a high resolution image output that dates back
     : to DEC VT200 and VT340 terminals. This image mode provides full
     : resolution on a 256 color palette that `timg` optimizes for each image.
     : You find the sixel protocol implemented by `xterm` (invoke
     : with `-ti vt340`) and `mlterm` or `konsole`. Recently, more terminal
     : emulators re-discovered this format and started implementing it.
     : This does not work in `tmux`, but there is a tmux fork with sixel
     : support around.

     **kitty** (short 'k')
     : The Kitty terminal implements an image protocol that allows for full
     : 24Bit RGB/32 Bit RGBA images to be displayed. This is implemented in the
     : `kitty` terminal but also e.g. `konsole`.
     : You can even use this in `tmux`: This is the only protocol that can
     : work around the reluctance of `tmux` to allow graphics protocols.
     : Some creative workarounds (Unicode placeholders) are used that are
     : only implemented in `kitty` version >= 0.28 right now. Also needs `tmux`
     : version >= 3.3. You have to explicitly set the `-pk` option inside
     : tmux as timg would otherwise just use block-pixels there.

     **iterm2** (short 'i')
     : The iterm2 graphics is another image protocol that allows for full
     : 24 Bit RGB/32 Bit RGBA images. It originated on the popular macOS
     : OpenSource iTerm2 terminal but is now also implemented by `wezterm` and
     : `konsole` as well as in the `VSCode`-terminal (enable in vscode settings
     : at checkbox 'Terminal > Integrated: Enable images').

    If no option is given, default is taken from environment variable
    **TIMG_PIXELATION**. If that is not set, `timg` attempts to
    auto-detect the available terminal feature. Not all full-resolution
    compatible terminals can be auto-detected so it will fall back
    to **quarter** in that case. Consider passing the `-p`
    option or set the `TIMG_PIXELATION` environment variable in that case.

**-\-grid**=&lt;*cols*&gt;[x&lt;*rows*&gt;]
:    Arrange images in a grid. If only one parameter is given, arranges in a
    square grid (e.g. **-\-grid=3** makes a 3x3 grid). Alternatively, you can
    choose columns and rows that should fit on one terminal
    (e.g. **-\-grid=3x2**).
    This is a very useful option if you want to browse images (see examples
    below).

**-C**, **-\-center**
:    Center image(s) and title(s) horizontally in their alotted space.

**-\-title**[=&lt;*format-string*&gt;]
:    Print title above each image. It is possible to customize the
     title by giving a format string. In this string, the following format
     specifiers are expanded:

       * `%f` = full filename
       * `%b` = basename (filename without path)
       * `%w` = image width
       * `%h` = image height
       * `%D` = internal decoder used (image, video, qoi, sta, openslide, ...)

     If no format string is given, this is just the filename (`%f`) or, if
     set, what is provided in the `TIMG_DEFAULT_TITLE` environment variable.

**-f** &lt;*filelist-file*&gt;
:    Read a list of image filenames to show from this file. The list needs
     to be newline separated, so one filename per line.
     This option can be supplied multiple times in which case it appends
     to the end of the list of images to show.
     If there are also filenames on the command line, they will also be
     shown after the images from the file list have been shown.

     Absolute filenames in the list are used as-is, relative filenames are
     resolved relative to the _current directory_.

     (Note: this behavior changed between v1.5.0 and v1.5.1: previously, -f
     was resolving relative to the filelist; this changed to current directory.
     Look-up relative to the file list is provided with with uppercase **-F**).

**-F** &lt;*filelist-file*&gt;
:    Like **-f**, but relative filenames are resolved relative to the
     _directory the file list resides in_.
     This allows you to e.g. have a file list at the top of a directory
     hierarchy with relative filenames but are not required to change into that
     directory first for `timg` to resolve the relative paths.

**-b** &lt;*background-color*&gt;
:    Set the background color for transparent images. Common HTML/SVG/X11
     color strings are supported, such as **purple**,
     **#00ff00** or **rgb(0, 0, 255)**.

     The special value **none** switches off blending background color and
     relies on the terminal to provide alpha-blending. This works well with
     kitty and iterm2 graphics, but might result in less blended edges for the
     text-block based pixelations.

     Another special value is **auto**:

       * For graphics modes, this behaves like `none`, sending RGBA
         images for alpha-blending directly in the terminal.
       * For text-block modes, this attempts to query the
         terminal for its background color (Best effort; not all terminals
         support that). If detection fails, the fallback is 'black'.

    Default is **auto**.

**-B** &lt;*checkerboard-other-color*&gt;
:   Show the background of a transparent image in a checkerboard pattern with
    the given color, which alternates with the **-b** color.
    The allows for HTTML/SVG/X11 colors like **-b**.

    The checkerboard pattern has square blocks one character cell wide and half
    a cell high (see `--pattern-size` to change).

    A common combination would be to use `-bgray -Bdarkgray` for backgrounds
    known from image editors.

    Sometimes setting such background is the only way to see an image, e.g.
    if you have an image with a transparent background showing content with
    the same color as your terminal background...

**-\-pattern-size**=&lt;*size-factor*&gt;
:   Scale background checkerboard pattern by this factor.

**-\-auto-crop**[=&lt;*pre-crop*&gt;]
:   Trim same-color pixels around the border of image before displaying. Use
    this if there is a boring even-colored space aorund the image which uses
    too many of our available few pixels.

    The optional pre-crop is number of pixels to unconditionally trim
    all around the original image, for instance to remove a thin border. The
    link in the [EXAMPLES](#EXAMPLES) section shows an example how this
    improves showing an xkcd comic with a border.

**-\-rotate**=&lt;*exif*|*off*&gt;
:   If 'exif', rotate the image according to the exif data stored
    in the image. With 'off', no rotation is extracted or applied.

**-W**, **-\-fit-width**
:    Scale to fit width of the available space. This means that the height can
     overflow, e.g. be longer than the terminal, so might require scrolling to
     see the full picture.
     Default behavior is to fit within the allotted width *and* height.

**-U**, **-\-upscale**[=i]
:   Allow Upscaling. If an image is smaller than the terminal size, scale
    it up to fit the terminal.

    By default, larger images are only scaled down and images smaller than the
    available pixels in the terminal are left at the original size (this
    helps assess small deliberately pixelated images such as icons in their
    intended appearance). This option scales up smaller images to fit available
    space (e.g. icons).

    The long option allows for an optional parameter **-\-upscale=i**
    that forces the upscaling to be in integer increments to keep the 'blocky'
    appearance of an upscaled image without bilinear scale 'fuzzing'.

**-\-clear**[=every]
:   Clear screen before *first* image. This places the image at the top of the
    screen.

    There is an optional parameter '*every*' (**-\-clear=every**), which will
    clean the screen before every image. This only makes sense if there is no
    **-\-grid** used and if you allow some time to show the image of course,
    so good in combination with **-w**.

**-V**
:   Tell `timg` that this is a video, directly read the content as video
    and don't attempt to probe image decoding first.

    Usually, `timg` will first attempt to interpret the data as image, but
    if it that fails, will fall-back to try interpret the file as video.
    However, if the file is coming from stdin, the first bytes used to probe
    for the image have already been consumed so the fall-back would fail in
    that case... Arguably, this should be dealt with automatically but isn't :)

    Long story short: if you read a video from a pipe, use **-V**.
    See link in [EXAMPLES](#EXAMPLES) section for a an example.

**-I**
:    This is an image, don't attempt to fall back to video decoding. Somewhat
    the opposite of **-V**.

**-w**&lt;*seconds*&gt;
:   Wait time in seconds between images when multiple images are given on
    the command line. Fractional values such as `-w0.3` are allowed.

**-wr**&lt;*seconds*&gt;
:   Similar to `-w`, but wait time between _rows_. If a `--grid` is chosen,
    this will wait at the end of a completed row. If no grid is chosen,
    then this is equivalent to `-w`. Both, `-w` and `-wr` can be provided to
    show each image individually, but also have a wait time between rows.

**-a**
:   Switch off anti-aliasing. The images are scaled down to show on the
    minimal amount of pixels, so some smoothing is applied for best visual
    effect. This option switches off that smoothing.

**-g** *&lt;width&gt;x&lt;height&gt;*
:    Geometry. Scale output to fit inside given number of character cells.
     By default, the size is determined by the available space in the terminal,
     so you typically won't have to change this.
     The image is scaled to fit inside the available box to fill the screen;
     see **-W** if you want to fill the width.

     It is possible to only partially specify the size before or after the
     `x`-separator, like **-g&lt;width&gt;x** or **-gx&lt;height&gt;**. The corresponding
     other value is then derived from the terminal size.

**-o** &lt;*outfile*&gt;
:    Write terminal image to given filename instead of stdout.

**-E**
:    Don't hide the cursor while showing images.

**-\-compress**[=&lt;*level*&gt;]
:   For the kitty and iterm2 graphics modes: this chooses the compression
    for the transmission to the terminal. This uses more CPU on timg, but is
    desirable when connected over a slow network.
    Default compression level is 1 which should be reasonable default in
    almost all cases. To disable, set to 0 (zero). Use `--verbose` to see
    the amount of data `timg` sent to the terminal.

**-\-threads**=&lt;*n*&gt;
:    Run image decoding in parallel with n threads. By default, up to 3/4 of
     the reported CPU-cores are used.

**-\-color8**
:   For `half` and `quarter` block pixelation: Use 8 bit color mode for
    terminals that don't support 24 bit color
    (only shows 6x6x6 = 216 distinct colors instead of 256x256x256 = 16777216).

**-\-version**
:    Print version and exit.

**-\-verbose**
:    Print some useful information such as observed terminal cells,
     chosen pixelation, or observed frame-rate.

**-h**
:    Print command line option help and exit.

**-\-help**
:    Page through detailed manpage-like help and exit.

**-\-debug-no-frame-delay**
:    Don't delay frames in videos or animations but emit as fast as possible.
     This might be useful for developers of terminal emulations to do
     performance tests or simply if you want to redirect the output to a file
     and don't want to wait.

## For Animations, Scrolling, or Video

Usually, animations are shown in full in an infinite loop. These options
limit infinity.

**-t**&lt;*seconds*&gt;
:   Stop an animation after these number of seconds.
    Fractional values are allowed.

**-\-loops**=&lt;*num*&gt;
:   Number of loops through a fully cycle of an animation or video.
    A value of *-1* stands for 'forever'.

    If *not* set, videos loop once, animated images forever unless there is
    more than one file to show. If there are multiple files on the command line,
    animated images are only shown once if **-\-loops** is not set to prevent
    the output get stuck on the first animation.

**-\-frames**=&lt;*frame-count*&gt;
:   Only render the first *frame-count* frames in an animation or video.
    If frame-count is set to 1, the output just is the first frame so
    behaves like a static image. Typically you'd use it when you show a bunch
    of images to quickly browse without waiting for animations to finish.

**-\-frame-offset**=&lt;*offset*&gt;
:    For animations or videos, start at this frame.

## Scrolling

**-\-scroll**[=&lt;*ms*&gt;]
:    Scroll horizontally with an optional delay between updates (default: 60ms).
     In the [EXAMPLES](#EXAMPLES) section is an example how to use ImageMagick
     to create a text that you then can scroll with **timg** over the terminal.

**-\-delta-move**=&lt;*dx*&gt;\:&lt;*dy*&gt;
:    Scroll with delta x and delta y. The default of 1:0 scrolls it horizontally,
    but with this option you can scroll vertically or even diagonally.

# RETURN VALUES

Exit code is

**0**
:    On reading and displaying all images successfully.

**1**
:    If any of the images could not be read or decoded or if there was no
    image provided.

**2**
:    If an invalid option or parameter was provided.

**3**
:    If `timg` could not determine the size of terminal (not a tty?). Provide
    **-g** option to provide size of the output to be generated.

**4**
:    Could not write to output file provided with **-o**.

**5**
:    Could not read file list file provided with **-f**.


# ENVIRONMENT

**TIMG_DEFAULT_TITLE**
:   The default format string used for `--title`. If not given, the default
    title format string is \"`%f`\".

**TIMG_PIXELATION**
:   The default pixelation if not provided by the `-p` or `--pixelation`
    option (see choice of values there). If neither the environment variable
    nor the option is given, timg attempts to auto-detect the best pixelation
    for the terminal.

**TIMG_USE_UPPER_BLOCK**
:    If this environment variable is set to the value **1**, `timg` will use the
     U+2580 - 'Upper Half Block' Unicode character.

    To display pixels, `timg` uses a Unicode half block and sets the foreground
    color and background color to get two vertical pixels. By default, it uses
    the U+2584 - 'Lower Half Block' character to achieve this goal. This
    has been chosen as it resulted in the best image in all tested terminals
    (konsole, gnome terminal and cool-retro-term). So usually, there is no
    need to change that. But if the terminal or font result in a funny output,
    this might be worth a try. This is an environment variable because if it
    turns out to yield a better result on your system, you can set it once
    in your profile and forget about it.

**TIMG_FONT_WIDTH_CORRECT**
:   A floating point stretch factor in width direction to correct for fonts
    that don't produce quite square output.

    If you notice that the image displayed is not quite the right aspect
    ratio because of the font used, you can modify this factor to make it
    look correct. Increasing the visual width by 10% would be setting it to
    *TIMG_FONT_WIDTH_CORRECT=1.1* for instance.

    This is an environment variable, so that you can set it once to best fit
    your terminal emulator of choice.

**TIMG_ALLOW_FRAME_SKIP**
:   Set this environment variable to 1 if you like to allow `timg` to drop
    frames when play-back is falling behind.
    This is particularly useful if you are on a very slow remote terminal
    connection that can't keep up with playing videos. Or if you have a very
    slow CPU.

# EXAMPLES

Some example invocations including scrolling text or streaming an
online video are put together at <https://timg.sh/#examples>

It might be useful to prepare some environment variables or aliases in the
startup profile of your shell. The `timg` author typically has these set:

```
# The default --title format
export TIMG_DEFAULT_TITLE="%b (%wx%h)"

# image list. An alias to quickly list images; invoke with ils images/*
alias ils='timg --grid=3x1 --upscale=i --center --title --frames=1 -bgray -Bdarkgray'
```

# KNOWN ISSUES

This requires a terminal that can deal with Unicode characters and 24 bit
color escape codes. This will be problematic on really old installations or
if you want to display images on some limited text console.

The option **-V** should not be necessary for streaming video from stdin;
timg should internally buffer bytes it uses for probing.

# BUGS

Report bugs at <http://github.com/hzeller/timg/issues>

# COPYRIGHT

Copyright (c) 2016..2023 Henner Zeller. This program is free software,
provided under the GNU GPL version 2.0.

<https://gnu.org/licenses/gpl-2.0.html>

# SEE ALSO

GraphicsMagick, ffmpeg(1), utf-8(7), unicode(7), kitty(1),
https://en.wikipedia.org/wiki/Sixel
