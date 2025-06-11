# FindGStreamer
# -------------
# Finds the GStreamer library
#
# This will define the following variables::
#
# GSTREAMER_FOUND - system has GStreamer
# GSTREAMER_INCLUDE_DIRS - the GStreamer include directory
# GSTREAMER_LIBRARIES - the GStreamer libraries
find_package(PkgConfig ${SEARCH_QUIET})
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_GSTREAMER
        gstreamer-1.0
        #gstreamer1.0-libav
        ${SEARCH_QUIET}
        )
  pkg_check_modules(PC_GSTPLUGINS
        gstreamer-video-1.0
        gstreamer-audio-1.0
        gstreamer-pbutils-1.0
        gstreamer-app-1.0 # for gst_app_sink_pull_sample
        ${SEARCH_QUIET}
        )
endif()

find_path(GSTREAMER_INCLUDE_DIR NAMES gstreamer-1.0/gst/gst.h
                                HINTS ${DEPENDS_PATH}/include ${PC_GSTREAMER_INCLUDEDIR}
                                ${${CORE_PLATFORM_LC}_SEARCH_CONFIG} NO_CACHE)

find_path(GSTREAMER_VIDEO_INCLUDE_DIR NAMES gstreamer-1.0/gst/video/video-frame.h
                                      HINTS ${DEPENDS_PATH}/include ${PC_GSTREAMER_INCLUDEDIR}
                                      ${${CORE_PLATFORM_LC}_SEARCH_CONFIG} NO_CACHE)

find_library(GSTREAMER_LIBRARY  NAMES gstreamer-1.0
                                PATHS ${PC_GSTREAMER_LIBDIR} NO_CACHE)
find_library(GSTREAMER_VIDEO_LIBRARY  NAMES gstvideo-1.0
                                      PATHS ${PC_GSTREAMER_LIBDIR} NO_CACHE)
find_library(GSTREAMER_AUDIO_LIBRARY  NAMES gstaudio-1.0
                                      PATHS ${PC_GSTREAMER_LIBDIR} NO_CACHE)
find_library(GSTREAMER_PBUTILS_LIBRARY  NAMES gstpbutils-1.0
                                        PATHS ${PC_GSTREAMER_LIBDIR} NO_CACHE)
find_library(GSTREAMER_APP_LIBRARY      NAMES gstapp-1.0
                                        PATHS ${PC_GSTREAMER_LIBDIR} NO_CACHE)

if(PC_GSTREAMER_FOUND)
  set(GSTREAMER_LIBRARIES ${GSTREAMER_LIBRARY} ${GSTREAMER_VIDEO_LIBRARY}
                          ${GSTREAMER_AUDIO_LIBRARY} ${GSTREAMER_PBUTILS_LIBRARY}
                          ${GSTREAMER_APP_LIBRARY})
  set(GSTREAMER_INCLUDE_DIRS ${PC_GSTREAMER_INCLUDE_DIR})
  if(PC_GSTREAMER_INCLUDE_DIRS)
    list(APPEND GSTREAMER_INCLUDE_DIRS ${PC_GSTREAMER_INCLUDE_DIRS})
  endif()
  # Make it available to our compiler
  #add_definitions(-DGSTREAMER_FOUND=1)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GStreamer
                                  REQUIRED_VARS GSTREAMER_LIBRARY
                                                GSTREAMER_VIDEO_LIBRARY
                                                GSTREAMER_AUDIO_LIBRARY
                                                GSTREAMER_PBUTILS_LIBRARY
                                                GSTREAMER_APP_LIBRARY
                                                GSTREAMER_INCLUDE_DIRS)

if(GSTREAMER_FOUND)
  add_library(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} PROPERTIES
                                                                   IMPORTED_LOCATION "${GSTREAMER_LIBRARY}"
                                                                   INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_INCLUDE_DIRS}"
                                                                   INTERFACE_COMPILE_DEFINITIONS HAVE_GSTREAMER)


  add_library(${APP_NAME_LC}::gstvideo UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gstvideo PROPERTIES
                                             IMPORTED_LOCATION "${GSTREAMER_VIDEO_LIBRARY}"
                                             INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_VIDEO_INCLUDE_DIR}")

  add_library(${APP_NAME_LC}::gstaudio UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gstaudio PROPERTIES
                                             IMPORTED_LOCATION "${GSTREAMER_AUDIO_LIBRARY}"
                                             INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_INCLUDE_DIRS}")

  add_library(${APP_NAME_LC}::gstpbutils UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gstpbutils PROPERTIES
                                             IMPORTED_LOCATION "${GSTREAMER_PBUTILS_LIBRARY}"
                                             INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_INCLUDE_DIRS}")

  add_library(${APP_NAME_LC}::gstapp UNKNOWN IMPORTED)
  set_target_properties(${APP_NAME_LC}::gstapp PROPERTIES
                                             IMPORTED_LOCATION "${GSTREAMER_APP_LIBRARY}"
                                             INTERFACE_INCLUDE_DIRECTORIES "${GSTREAMER_INCLUDE_DIRS}")

  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gstvideo)
  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gstaudio)
  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gstpbutils)
  target_link_libraries(${APP_NAME_LC}::${CMAKE_FIND_PACKAGE_NAME} INTERFACE ${APP_NAME_LC}::gstapp)

  set(GSTREAMER_INCLUDE_DIRS ${GSTREAMER_INCLUDE_DIRS} CACHE INTERNAL "")
endif()
