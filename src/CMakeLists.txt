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
  utils.h           utils.cc
  term-query.h      term-query.cc
  thread-pool.h
  timg-base64.h
  timg-png.h        timg-png.cc
  timg-time.h
  timg-help.h       timg-help.cc
  unicode-block-canvas.h unicode-block-canvas.cc
)

target_link_libraries(timg Threads::Threads)

if (LIBDEFLATE_PKGCONFIG_FOUND)
  target_link_libraries(timg PkgConfig::LIBDEFLATE_PKGCONFIG)
else()
  # Use fallback.
  target_link_libraries(timg ${LIBDEFLATE_LIBRARY})
endif()

target_include_directories(timg PRIVATE ${CMAKE_BINARY_DIR}/src)

target_compile_features(timg PRIVATE cxx_std_17)

if(TIMG_VERSION_FROM_GIT)
  git_describe(GIT_DESCRIBE_VERSION)
  git_committime(GIT_DATE)
endif()
if(NOT GIT_DESCRIBE_VERSION OR NOT TIMG_VERSION_FROM_GIT)
  unset(GIT_DATE)
  set(GIT_DESCRIBE_VERSION "${PROJECT_VERSION}+")
endif()

configure_file(timg-version.h.in timg-version.h)


if(WITH_LIBSIXEL)
  target_sources(timg PUBLIC sixel-canvas.h sixel-canvas.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_SIXEL)
  target_link_libraries(timg PkgConfig::LIBSIXEL)
endif()

if(WITH_GRAPHICSMAGICK)
  target_sources(timg PUBLIC graphics-magick-source.h graphics-magick-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_GRPAPHICSMAGICK)
  target_link_libraries(timg PkgConfig::GRAPHICSMAGICKXX)
endif()

if(WITH_RSVG)
  target_sources(timg PUBLIC svg-image-source.h svg-image-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_RSVG)
  target_link_libraries(timg PkgConfig::RSVG PkgConfig::CAIRO)
endif()

if(WITH_POPPLER)
  target_sources(timg PUBLIC pdf-image-source.h pdf-image-source.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_POPPLER)
  target_link_libraries(timg PkgConfig::POPPLER PkgConfig::CAIRO)
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
  target_sources(timg PUBLIC video-source.h video-source.cc)
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
  # STB usuall does not come with pkg-config. Just check if we see the include
  include(CheckIncludeFileCXX)
  CHECK_INCLUDE_FILE_CXX(stb/stb_image.h HAVE_STB)
  target_sources(timg PUBLIC stb-image-source.h stb-image-source.cc)
  if(NOT HAVE_STB)
    message("--> Using STB from third_party/ instead")
    target_include_directories(timg PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
  endif()
  CHECK_INCLUDE_FILE_CXX(stb/stb_image_resize2.h STB_RESIZE_VERSION2)
  if(STB_RESIZE_VERSION2)
    target_compile_definitions(timg PUBLIC STB_RESIZE_VERSION2)
  endif()
  target_compile_definitions(timg PUBLIC WITH_TIMG_STB)
endif()

if(WITH_QOI_IMAGE)
  include(CheckIncludeFileCXX)
  CHECK_INCLUDE_FILE_CXX(qoi.h HAVE_QOI)
  target_sources(timg PUBLIC qoi-image-source.h qoi-image-source.cc)
  if(NOT HAVE_QOI)
    message("--> Using QOI from third_party/ instead")
    target_include_directories(timg PRIVATE ${CMAKE_SOURCE_DIR}/third_party/qoi)
  endif()
  target_compile_definitions(timg PUBLIC WITH_TIMG_QOI)
  target_link_libraries(timg
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE)
endif()

# We always take the manpage from the checkout currently so that we don't
# require pandoc to build.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/timg-manpage.inc
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/create-manpage-inc.sh" ${CMAKE_CURRENT_SOURCE_DIR}/../man/timg.1 ${CMAKE_CURRENT_BINARY_DIR}/timg-manpage.inc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../man/timg.1
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating builtin manpage")
add_custom_target(builtin-manpage DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/timg-manpage.inc)

# Currently disabled. The ancient `man` command on MacOS is misbehaving, so
# for now, it is checked into the repo at src/timg-manpage.inc
#add_dependencies(timg builtin-manpage)


install(TARGETS timg RUNTIME DESTINATION bin)
