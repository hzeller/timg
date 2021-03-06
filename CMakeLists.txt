cmake_minimum_required(VERSION 3.10)

project(timg VERSION 1.4.2 LANGUAGES CXX)

option(WITH_VIDEO_DECODING "Enables video decoding feature" ON)
option(WITH_VIDEO_DEVICE "Enables reading videos from devices e.g. v4l2" ON)
option(WITH_OPENSLIDE_SUPPORT "Enables support to scientific OpenSlide formats" ON)

# Options that should be typically on, but could be disabled for special
# applications where less dependencies are required
option(WITH_GRAPHICSMAGICK "Enable general image loading with Graphicsmagick. You typically want this." ON)
option(WITH_TURBOJPEG "Optimized JPEG loading. You typically want this." ON)

# Note: The version string can be ammended with -DDISTRIBUTION_VERSION, see src/timg-version.h.in
option(TIMG_VERSION_FROM_GIT "Get the program version from the git repository" ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(FindPkgConfig)
include(GNUInstallDirs)

if(TIMG_VERSION_FROM_GIT)
  find_package(Git REQUIRED)
  include(GetGitRevisionDescription)
endif()

pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE IMPORTED_TARGET REQUIRED libswscale)

if(WITH_TURBOJPEG)
  pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
  pkg_check_modules(EXIF REQUIRED IMPORTED_TARGET libexif)
endif()

if(WITH_GRAPHICSMAGICK)
  pkg_check_modules(GRAPHICSMAGICKXX IMPORTED_TARGET REQUIRED GraphicsMagick++)
endif()

if(WITH_OPENSLIDE_SUPPORT)
  pkg_check_modules(OPENSLIDE IMPORTED_TARGET REQUIRED openslide)
endif()

if(WITH_VIDEO_DECODING)
  pkg_check_modules(LIBAV IMPORTED_TARGET REQUIRED libavcodec libavutil libavformat)
  if (WITH_VIDEO_DEVICE)
    pkg_check_modules(LIBAV_DEVICE IMPORTED_TARGET REQUIRED libavdevice)
  endif()
endif()

find_package(Threads)

add_subdirectory(src)
add_subdirectory(man)
