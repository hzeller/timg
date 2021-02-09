cmake_minimum_required(VERSION 3.13)

project(timg VERSION 0.9.9 LANGUAGES CXX)

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

if(WITH_VIDEO_DECODING)
  pkg_check_modules(LIBAV IMPORTED_TARGET REQUIRED libavcodec libavutil libavformat)
  pkg_check_modules(WSCALE IMPORTED_TARGET REQUIRED libswscale)
endif()

find_package(Threads)

add_subdirectory(src)
add_subdirectory(man)
