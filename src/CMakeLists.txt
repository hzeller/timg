add_executable(timg)
target_sources(timg PRIVATE image-display.cc terminal-canvas.cc timg.cc)
target_link_libraries(timg PkgConfig::GRAPHICSMAGICKXX)

# instead of just telling cmake "c++11, please", this a more modern approach: 
# https://cmake.org/cmake/help/latest/prop_gbl/CMAKE_CXX_KNOWN_FEATURES.html
# https://crascit.com/2015/03/28/enabling-cxx11-in-cmake/ "Setting the C++ standard based on features"
target_compile_features(timg PRIVATE cxx_auto_type cxx_constexpr cxx_nullptr cxx_range_for)

if(TIMG_VERSION_FROM_GIT)
  target_compile_definitions(timg PRIVATE HAVE_CMAKE_GENERATED_VERSION_H)
  git_describe(GIT_DESCRIBE_VERSION)
  git_committime(GIT_DATE)
  configure_file(timg-version-generated.h.in timg-version-generated.h)
else()
  target_compile_definitions(timg PRIVATE TIMG_VERSION="${PROJECT_VERSION}+")
endif()
target_include_directories(timg PRIVATE ${CMAKE_BINARY_DIR}/src)
configure_file(timg-version.h.in timg-version.h)  # Compatibilty layer for src/Makefile.


if(WITH_VIDEO_DECODING)
  target_sources(timg PUBLIC video-display.cc)
  target_compile_definitions(timg PUBLIC WITH_TIMG_VIDEO)
  target_link_libraries(timg PkgConfig::LIBAV PkgConfig::WSCALE)
endif()

install(TARGETS timg)
