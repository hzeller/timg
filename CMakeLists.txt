# Minimum version of cmake
cmake_minimum_required (VERSION 3.0)

# Name of the project
project (timg)

# We require C++-98
set(CMAKE_CXX_STANDARD 98)

# Add a new cmake module directory, which has FindGraphicsMagick++.cmake
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules)
# Load the module, it is a required module so stop if you cannot find it.
find_package(GraphicsMagick++ REQUIRED)
# The module has the following macro. Let's run it.
# The macro takes four parameters. These parameters are filled in *by the macro*.
graphicsmagickplusplusconfig(MAGICK++_INCLUDE_DIR MAGICK++_LINK_DIR 
    MAGICK++_LINK_LIBS MAGICK++_CFLAGS)

# Set a variable called ${SOURCE_FILES}, which has the names of the two source files.
set(SOURCE_FILES src/timg.cc src/terminal-canvas.cc)
# Specify that the executable is called "timg" and it is depending on ${SOURCE_FILES}.
add_executable(timg ${SOURCE_FILES})
# Set the include (-I) directories to the result that the macro gave us earlier.
target_include_directories(timg PRIVATE ${MAGICK++_INCLUDE_DIR})
# Set the libraries (-l) to the result that the macro gave us earlier.
target_link_libraries(timg ${MAGICK++_LINK_LIBS})

install(TARGETS timg DESTINATION bin)
