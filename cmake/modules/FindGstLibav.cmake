#.rst:
# FindGstLibav
# ------------
# Finds GstLibav
#
# This will define the following variables:
#
# GSTLIBAV_FOUND        - System has GstLibav
# GSTLIBAV_INCLUDE_DIRS - the GstLibav include directory
#

# libav is located in xbmc/contrib/libav
set(GSTLIBAV_FOUND TRUE)
set(GSTLIBAV_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/xbmc/contrib")
message(STATUS "Found GstLibav: ${GSTLIBAV_INCLUDE_DIRS}")
