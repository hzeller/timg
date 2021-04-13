set(CMAKE_CXX_FLAGS "-W -Wall -Wextra -Wno-unused-parameter -O3")

add_executable(timg)
target_sources(timg PRIVATE
  timg.cc

  buffered-write-sequencer.h buffered-write-sequencer.cc
  display-options.h
  framebuffer.h     framebuffer.cc
  image-display.h   image-display.cc
  image-source.h    image-source.cc
  iterm2-canvas.h   iterm2-canvas.cc
  jpeg-source.h     jpeg-source.cc
  kitty-canvas.h    kitty-canvas.cc
  renderer.h        renderer.cc
  terminal-canvas.h terminal-canvas.cc
  termutils.h       termutils.cc
  thread-pool.h
  timg-base64.h
  timg-png.h        timg-png.cc
  timg-time.h
  unicode-block-canvas.h unicode-block-canvas.cc
)

target_link_libraries(timg
  PkgConfig::GRAPHICSMAGICKXX
  PkgConfig::EXIF
  PkgConfig::TURBOJPEG
  PkgConfig::SWSCALE
  PkgConfig::AVUTIL
  PkgConfig::ZLIB
  Threads::Threads)
target_include_directories(timg PRIVATE ${CMAKE_BINARY_DIR}/src)

target_compile_features(timg PRIVATE cxx_std_14)

if(TIMG_VERSION_FROM_GIT)
  git_describe(GIT_DESCRIBE_VERSION)
  git_committime(GIT_DATE)
endif()
if(NOT GIT_DESCRIBE_VERSION OR NOT TIMG_VERSION_FROM_GIT)
  unset(GIT_DATE)
  set(GIT_DESCRIBE_VERSION "${PROJECT_VERSION}+")
endif()

configure_file(timg-version.h.in timg-version.h)

if(WITH_OPENSLIDE_SUPPORT)
  target_sources(timg PUBLIC openslide-source.cc openslide-source.h)
  target_compile_definitions(timg PUBLIC WITH_OPENSLIDE_SUPPORT)
  target_link_libraries(timg PkgConfig::OPENSLIDE)
endif()

if(WITH_VIDEO_DECODING)
  target_sources(timg PUBLIC video-display.cc video-display.h)
  target_compile_definitions(timg PUBLIC WITH_TIMG_VIDEO)
  target_link_libraries(timg PkgConfig::LIBAV PkgConfig::SWSCALE)
  if (WITH_VIDEO_DEVICE)
    target_link_libraries(timg PkgConfig::LIBAV_DEVICE PkgConfig::SWSCALE)
    add_definitions(-DHAVE_AVDEVICE)
  endif()
endif()

install(TARGETS timg
  RUNTIME DESTINATION bin
)

# Local debian package creation until available natively in Debian.
# Use
#   cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ../
#   cpack
SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "henner.zeller+unsupported-timg-deb@gmail.com")
SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION "A terminal image and video viewer.")
SET(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://timg.sh/")
SET(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
INCLUDE(CPack)
