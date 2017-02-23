# FindGraphicsMagicki++
# ------------
#
# GraphicsMagick++-config module for CMake.
#
# This module defines the following macro:
#
# GRAPHICSMAGICKPLUSPLUSCONFIG(includedir libdir linklibs cflags)
#
# When you run the macro in your CMakeLists.txt, the four parameters
# are filled in with the appropriate valued from GraphicsMagick++-config

macro(GRAPHICSMAGICKPLUSPLUSCONFIG _include_DIR _link_DIR _link_LIBS _cflags)
# reset the variables at the beginning
  set(${_include_DIR})
  set(${_link_DIR})
  set(${_link_LIBS})
  set(${_cflags})

  # Try to find the executable.
  find_program(GMXXCONFIGEXECUTABLE NAMES GraphicsMagick++-config )

  # If it has not been found, we need to install the following package.
  if (NOT GMXXCONFIGEXECUTABLE)
    message(FATAL_ERROR "The command GraphicsMagick++-config has not been found. Please install the package libgraphicsmagick++1-dev and try again")
  endif ()

  # The following returns a string like "-I/usr/include/GraphicsMagick++".
  # cmake does not like the "-I" so we remove it in the next REGEX REPLACE.
  exec_program(${GMXXCONFIGEXECUTABLE} ARGS --cppflags OUTPUT_VARIABLE ${_include_DIR} )
  string(REGEX REPLACE "-I" "" ${_include_DIR} "${${_include_DIR}}")

  exec_program(${GMXXCONFIGEXECUTABLE} ARGS --ldflags OUTPUT_VARIABLE ${_link_DIR} )

  exec_program(${GMXXCONFIGEXECUTABLE} ARGS --libs OUTPUT_VARIABLE ${_link_LIBS} )

  exec_program(${GMXXCONFIGEXECUTABLE} ARGS --cxxflags OUTPUT_VARIABLE ${_cflags} )

endmacro()

mark_as_advanced(GRAPHICSMAGICKPLUSPLUSCONFIG)

