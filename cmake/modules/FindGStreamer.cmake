#.rst:
# FindGStreamer
# -------------
# Finds the GStreamer library
#
# This will define the following targets:
#
#   ${APP_NAME_LC}::GStreamer     - The GStreamer core library
#   ${APP_NAME_LC}::GStreamerApp  - The GStreamer app library (for GstAppSrc)

if(NOT TARGET ${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME})
  include(cmake/scripts/common/ModuleHelpers.cmake)

  SETUP_FIND_SPECS()

  find_package(PkgConfig ${SEARCH_QUIET})
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_GSTREAMER gstreamer-1.0 ${SEARCH_QUIET})
    pkg_check_modules(PC_GSTREAMER_APP gstreamer-app-1.0 ${SEARCH_QUIET})
  endif()

  # GStreamer core
  find_path(GSTREAMER_INCLUDE_DIR NAMES gst/gst.h
                                  HINTS ${PC_GSTREAMER_INCLUDEDIR}
                                  PATH_SUFFIXES gstreamer-1.0)

  find_library(GSTREAMER_LIBRARY NAMES gstreamer-1.0
                                 HINTS ${PC_GSTREAMER_LIBDIR})

  # GStreamer app
  find_path(GSTREAMER_APP_INCLUDE_DIR NAMES gst/app/gstappsrc.h
                                      HINTS ${PC_GSTREAMER_APP_INCLUDEDIR}
                                      PATH_SUFFIXES gstreamer-1.0)

  find_library(GSTREAMER_APP_LIBRARY NAMES gstapp-1.0
                                     HINTS ${PC_GSTREAMER_APP_LIBDIR})

  set(GSTREAMER_VERSION ${PC_GSTREAMER_VERSION})

  if(NOT VERBOSE_FIND)
    set(${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY TRUE)
  endif()

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(GStreamer
                                    REQUIRED_VARS GSTREAMER_LIBRARY 
                                                  GSTREAMER_INCLUDE_DIR
                                                  GSTREAMER_APP_LIBRARY
                                                  GSTREAMER_APP_INCLUDE_DIR
                                    VERSION_VAR GSTREAMER_VERSION)

  if(GSTREAMER_FOUND)
    # GStreamer core target
    add_library(${APP_NAME_LC}::GStreamer UNKNOWN IMPORTED)
    set_target_properties(${APP_NAME_LC}::GStreamer PROPERTIES
                                                    IMPORTED_LOCATION "${GSTREAMER_LIBRARY}"
                                                    INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_INCLUDE_DIR}")

    # GStreamer app target
    add_library(${APP_NAME_LC}::GStreamerApp UNKNOWN IMPORTED)
    set_target_properties(${APP_NAME_LC}::GStreamerApp PROPERTIES
                                                       IMPORTED_LOCATION "${GSTREAMER_APP_LIBRARY}"
                                                       INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_APP_INCLUDE_DIR}")
    set_property(TARGET ${APP_NAME_LC}::GStreamerApp APPEND PROPERTY
                 INTERFACE_LINK_LIBRARIES ${APP_NAME_LC}::GStreamer)

    target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::GStreamerApp)
  else()
    if(GStreamer_FIND_REQUIRED)
      message(FATAL_ERROR "GStreamer libraries not found.")
    endif()
  endif()
endif()
