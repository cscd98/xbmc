#.rst:
# FindGLib
# --------
# Finds the GLib library
#
# This will define the following target:
#
#   ${APP_NAME_LC}::GLib   - The GLib library

if(NOT TARGET ${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME})
  include(cmake/scripts/common/ModuleHelpers.cmake)

  SETUP_FIND_SPECS()

  find_package(PkgConfig ${SEARCH_QUIET})
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_GLIB glib-2.0 ${SEARCH_QUIET})
  endif()

  find_path(GLIB_INCLUDE_DIR NAMES glib.h
                             HINTS ${PC_GLIB_INCLUDEDIR}
                             PATH_SUFFIXES glib-2.0)
  
  find_path(GLIBCONFIG_INCLUDE_DIR NAMES glibconfig.h
                                   HINTS ${PC_GLIB_INCLUDEDIR} ${CMAKE_FIND_ROOT_PATH}
                                   PATH_SUFFIXES lib/glib-2.0/include ../lib/glib-2.0/include)

  find_library(GLIB_LIBRARY NAMES glib-2.0
                            HINTS ${PC_GLIB_LIBDIR})

  set(GLIB_VERSION ${PC_GLIB_VERSION})

  if(NOT VERBOSE_FIND)
    set(${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY TRUE)
  endif()

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(GLib
                                    REQUIRED_VARS GLIB_LIBRARY GLIB_INCLUDE_DIR GLIBCONFIG_INCLUDE_DIR
                                    VERSION_VAR GLIB_VERSION)

  if(GLIB_FOUND)
    add_library(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} UNKNOWN IMPORTED)
    set_target_properties(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} PROPERTIES
                                                                     IMPORTED_LOCATION "${GLIB_LIBRARY}"
                                                                     INTERFACE_INCLUDE_DIRECTORIES "${GLIB_INCLUDE_DIR};${GLIBCONFIG_INCLUDE_DIR}")
    
    # Add compile definitions if needed
    if(PC_GLIB_CFLAGS_OTHER)
      set_property(TARGET ${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} APPEND PROPERTY
                   INTERFACE_COMPILE_OPTIONS "${PC_GLIB_CFLAGS_OTHER}")
    endif()
  else()
    if(GLib_FIND_REQUIRED)
      message(FATAL_ERROR "GLib library not found.")
    endif()
  endif()
endif()
