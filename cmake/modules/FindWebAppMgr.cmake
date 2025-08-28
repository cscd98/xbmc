#.rst:
# WebAppMgr
# --------
# Finds the WebAppMgr library
#
# This will define the following target:
#
#   ${APP_NAME_LC}::WebAppMgr   - The WebAppMgr library

if(NOT TARGET ${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME})
  include(cmake/scripts/common/ModuleHelpers.cmake)

  SETUP_FIND_SPECS()

  find_package(PkgConfig ${SEARCH_QUIET})
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_WEBAPPMGR libWebAppMgr${PC_${CMAKE_FIND_PACKAGE_NAME}_FIND_SPEC} ${SEARCH_QUIET})
  endif()

  find_path(WEBAPPMGR_INCLUDE_DIR NAMES WebAppMgr/DeviceInfoTv.h
                                   HINTS ${PC_WEBAPPMGR_INCLUDEDIR})
  find_library(WEBAPPMGR_LIBRARY NAMES WebAppMgr
                                  HINTS ${PC_WEBAPPMGR_LIBDIR})

  set(WEBAPPMGR_VERSION ${PC_WEBAPPMGR_VERSION})

  if(NOT VERBOSE_FIND)
     set(${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY TRUE)
   endif()

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(WebAppMgr
                                    REQUIRED_VARS WEBAPPMGR_LIBRARY WEBAPPMGR_INCLUDE_DIR
                                    VERSION_VAR WEBAPPMGR_VERSION)

  if(WEBAPPMGR_FOUND)
    add_library(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} UNKNOWN IMPORTED)
    set_target_properties(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} PROPERTIES
                                                                     IMPORTED_LOCATION "${WEBAPPMGR_LIBRARY}"
                                                                     INTERFACE_INCLUDE_DIRECTORIES "${WEBAPPMGR_INCLUDE_DIR}")
  else()
    if(WebAppMgr_FIND_REQUIRED)
      message(FATAL_ERROR "WebAppMgr library not found.")
    endif()
  endif()
endif()
