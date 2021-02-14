cmake_minimum_required(VERSION 3.13)

project(timg VERSION 1.0.1 LANGUAGES CXX)

option(WITH_VIDEO_DECODING "Enables video decoding feature" ON)
# Note: The version string can be ammended with -DDISTRIBUTION_VERSION, see src/timg-version.h.in
option(TIMG_VERSION_FROM_GIT "Get the program version from the git repository" ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(FindPkgConfig)
include(GNUInstallDirs)

if(TIMG_VERSION_FROM_GIT)
  find_package(Git REQUIRED)
  include(GetGitRevisionDescription)
endif()

pkg_check_modules(GRAPHICSMAGICKXX IMPORTED_TARGET REQUIRED GraphicsMagick++)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
pkg_check_modules(EXIF REQUIRED IMPORTED_TARGET libexif)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE IMPORTED_TARGET REQUIRED libswscale)

if(WITH_VIDEO_DECODING)
  pkg_check_modules(LIBAV IMPORTED_TARGET REQUIRED libavcodec libavutil libavformat)
endif()

find_package(Threads)

add_subdirectory(src)
add_subdirectory(man)
