#.rst:
# FindGstLibav
# ------------
# Finds GstLibav
#
# This will define the following variables:
#
# GSTLIBAV_FOUND        - System has GstLibav
# GSTLIBAV_INCLUDE_DIR - the GstLibav include directory
#
#find_path(GSTLIBAV_INCLUDE_DIR NAMES gstavcodecmap.c
#                                     #PATH_SUFFIXES xbmc/contrib
#                                     HINTS xbmc/contrib/gstlibav
#                                     REQUIRED)
set(GSTLIBAV_FOUND TRUE)
set(GSTLIBAV_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/xbmc/contrib")

add_library(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE IMPORTED)
set_target_properties(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} PROPERTIES
                                                                 INTERFACE_INCLUDE_DIRECTORIES "${GSTLIBAV_INCLUDE_DIR}")

include(FindPackageMessage)
find_package_message(GstLibav "Found GstLibav: ${GSTLIBAV_INCLUDE_DIR}" "[${GSTLIBAV_INCLUDE_DIR}]")

mark_as_advanced(GSTLIBAV_INCLUDE_DIR)
