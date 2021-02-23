set(CMAKE_CXX_FLAGS "-W -Wall -Wextra -Wno-unused-parameter -O3")

add_executable(timg)
target_sources(timg PRIVATE
  timg.cc

  display-options.h
  image-display.h   image-display.cc
  image-source.h    image-source.cc
  jpeg-source.h     jpeg-source.cc
  renderer.h        renderer.cc
  terminal-canvas.h terminal-canvas.cc
  termutils.h       termutils.cc
  framebuffer.h     framebuffer.cc
  thread-pool.h
  timg-time.h
)

target_link_libraries(timg
  PkgConfig::GRAPHICSMAGICKXX
  PkgConfig::EXIF
  PkgConfig::TURBOJPEG
  PkgConfig::SWSCALE
  PkgConfig::AVUTIL
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
