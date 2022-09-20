set(CMAKE_CXX_FLAGS "-W -Wall -Wextra -Wno-unused-parameter -O3 ${CMAKE_CXX_FLAGS}")

add_executable(timg timg.cc)
target_sources(timg PRIVATE
  buffered-write-sequencer.h buffered-write-sequencer.cc
  display-options.h
  framebuffer.h     framebuffer.cc
  image-source.h    image-source.cc
  iterm2-canvas.h   iterm2-canvas.cc
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

target_link_libraries(timg PkgConfig::ZLIB Threads::Threads)
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

if(WITH_TERMINAL_QUERY)
  target_sources(timg PUBLIC term-query.h term-query.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_TERMINAL_QUERY)
endif()

if(WITH_GRAPHICSMAGICK)
  target_sources(timg PUBLIC image-display.h image-display.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_GRPAPHICSMAGICK)
  target_link_libraries(timg PkgConfig::GRAPHICSMAGICKXX)
endif()

if(WITH_TURBOJPEG)
  target_sources(timg PUBLIC jpeg-source.h jpeg-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_JPEG)
  target_link_libraries(timg
    PkgConfig::TURBOJPEG
    PkgConfig::EXIF
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE)
endif()

if(WITH_OPENSLIDE_SUPPORT)
  target_sources(timg PUBLIC openslide-source.h openslide-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_OPENSLIDE_SUPPORT)
  target_link_libraries(timg PkgConfig::OPENSLIDE
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE)
endif()

if(WITH_VIDEO_DECODING)
  target_sources(timg PUBLIC video-display.h video-display.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_VIDEO)
  target_link_libraries(timg
    PkgConfig::LIBAV
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE)
  if (WITH_VIDEO_DEVICE)
    target_link_libraries(timg PkgConfig::LIBAV_DEVICE)
    add_definitions(-DHAVE_AVDEVICE)
  endif()
endif()

if(WITH_STB_IMAGE)
  target_sources(timg PUBLIC stb-image-source.h stb-image-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_STB)
endif()

if(WITH_QOI_IMAGE)
  target_sources(timg PUBLIC qoi-image-source.h qoi-image-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_QOI)
  target_link_libraries(timg
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE)
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
