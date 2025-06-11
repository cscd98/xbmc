# FindGlib
# --------
# Finds the Glib library
#
# This will define the following variables::
#
# GLIB_FOUND - system has glib/gobject/gmodule
# GLIB_INCLUDE_DIRS - the glib include directory
# GSLIB_LIBRARIES - the glib libraries
find_package(PkgConfig ${SEARCH_QUIET})
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_GLIB
        glib-2.0
        ${SEARCH_QUIET}
        )
  pkg_check_modules(PC_GOBJECT
        gobject-2.0
        gmodule-2.0
        ${SEARCH_QUIET}
        )
endif()

find_path(GLIB_INCLUDE_DIR NAMES glib-2.0/glib.h
                           HINTS ${PC_GLIB_INCLUDEDIR}
                           NO_CACHE)

find_path(GOBJECT_INCLUDE_DIR NAMES gobject/gobject.h
                              HINTS ${DEPENDS_PATH}/include
                              ${PC_GOBJECT_INCLUDEDIR}
                              ${PC_GOBJECT_INCLUDE_DIRS}
                              NO_CACHE)

find_library(GLIB_LIBRARY       NAMES glib-2.0
                                PATHS ${PC_GLIB_LIBDIR} NO_CACHE)

find_library(GOBJECT_LIBRARY    NAMES gobject-2.0
                                PATHS ${PC_GOBJECT_LIBDIR} NO_CACHE)
                                
find_library(GMODULE_LIBRARY    NAMES gmodule-2.0
                                PATHS ${PC_GMODULE_LIBDIR} NO_CACHE)

find_library(GIO_LIBRARY        NAMES gio-2.0
                                PATHS ${PC_GIO_LIBDIR} NO_CACHE)

if(PC_GLIB_FOUND)
  set(GLIB_LIBRARIES ${GIO_LIBRARY} ${GOBJECT_LIBRARY} ${GMODULE_LIBRARY} ${GLIB_LIBRARY})
  set(GLIB_INCLUDE_DIRS ${PC_GLIB_INCLUDE_DIR})
  if(PC_GLIB_INCLUDE_DIRS)
    list(APPEND GLIB_INCLUDE_DIRS ${PC_GLIB_INCLUDE_DIRS})
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Glib
                                  REQUIRED_VARS GLIB_LIBRARY
                                                GMODULE_LIBRARY
                                                GOBJECT_LIBRARY
                                                GIO_LIBRARY
                                                GLIB_INCLUDE_DIR
                                                # GMODULE_INCLUDE_DIR
                                                # GIO_INCLUDE_DIR
                                                GOBJECT_INCLUDE_DIR)

if(GLIB_FOUND)
  find_library(FFI_LIBRARY ffi REQUIRED)

  add_library(${APP_NAME_LC}::gmodule UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gmodule PROPERTIES
                                                IMPORTED_LOCATION "${GMODULE_LIBRARY}"
                                                INTERFACE_INCLUDE_DIRECTORIES "${GLIB_INCLUDE_DIR}")

  add_library(${APP_NAME_LC}::gobject UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gobject PROPERTIES
                                                IMPORTED_LOCATION "${GOBJECT_LIBRARY}"
                                                INTERFACE_INCLUDE_DIRECTORIES "${GOBJECT_INCLUDE_DIR}")

  add_library(${APP_NAME_LC}::gio UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gio PROPERTIES
                                                IMPORTED_LOCATION "${GIO_LIBRARY}"
                                                INTERFACE_INCLUDE_DIRECTORIES "${GLIB_INCLUDE_DIR}")

  set(GLIB_LINK_LIBRARIES ${APP_NAME_LC}::gio ${APP_NAME_LC}::gobject ${APP_NAME_LC}::gmodule ${FFI_LIBRARY})
   
  add_library(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME}  PROPERTIES
                                                                    IMPORTED_LOCATION "${GLIB_LIBRARY}"
                                                                    INTERFACE_INCLUDE_DIRECTORIES "${GLIB_INCLUDE_DIR}")
  #                                                                  INTERFACE_LINK_LIBRARIES "${GLIB_LINK_LIBRARIES}")

  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gio)
  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gobject)
  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gmodule)

  set(GLIB_INCLUDE_DIRS ${GLIB_INCLUDE_DIRS} CACHE INTERNAL "")
endif()
