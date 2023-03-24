# FindGStreamer
# -------------
# Finds the GStreamer library
#
# This will define the following variables::
#
# GSTREAMER_FOUND - system has GStreamer
# GSTREAMER_INCLUDE_DIRS - the GStreamer include directory
# GSTREAMER_LIBRARIES - the GStreamer libraries
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_GSTLIBS QUIET
        gobject-2.0
        glib-2.0
        )
  pkg_check_modules(PC_GSTREAMER QUIET
        gstreamer-1.0 
        )
  pkg_check_modules(PC_GSTPLUGINS QUIET
        gstreamer-video-1.0
        gstreamer-audio-1.0
        gstreamer-pbutils-1.0
        gstreamer-app-1.0) # for gst_app_sink_pull_sample
endif()

find_path(GSTLIBS_INCLUDE_DIR NAMES gobject/gobject.h glib-2.0/glib.h pcre2.h
                              PATHS ${PC_GSTLIBS_INCLUDEDIR})
find_path(GSTREAMER_INCLUDE_DIR NAMES gstreamer-1.0/gst/gst.h
                                PATHS ${PC_GSTREAMER_INCLUDEDIR})

find_library(GOBJECT_LIBRARY    NAMES gobject-2.0
                                PATHS ${PC_GSTLIBS_LIBDIR})
find_library(GMODULE_LIBRARY    NAMES gmodule-2.0
                                PATHS ${PC_GSTLIBS_LIBDIR})
find_library(GLIB_LIBRARY       NAMES glib-2.0
                                PATHS ${PC_GSTLIBS_LIBDIR})
find_library(PCRE2_LIBRARY      NAMES pcre2-8
                                PATHS ${PC_GSTLIBS_LIBDIR})
find_library(GSTREAMER_LIBRARY  NAMES gstreamer-1.0
                                PATHS ${PC_GSTREAMER_LIBDIR})
find_library(GSTREAMER_VIDEO_LIBRARY  NAMES gstvideo-1.0
                                      PATHS ${PC_GSTREAMER_LIBDIR})
find_library(GSTREAMER_AUDIO_LIBRARY  NAMES gstaudio-1.0
                                      PATHS ${PC_GSTREAMER_LIBDIR})
find_library(GSTREAMER_PBUTILS_LIBRARY  NAMES gstpbutils-1.0
                                        PATHS ${PC_GSTREAMER_LIBDIR})
find_library(GSTREAMER_APP_LIBRARY      NAMES gstapp-1.0
                                        PATHS ${PC_GSTREAMER_LIBDIR})

if(PC_GSTREAMER_FOUND)
  set(GSTREAMER_LIBRARIES ${GOBJECT_LIBRARY} ${GMODULE_LIBRARY} ${GLIB_LIBRARY} 
                          ${PCRE2_LIBRARY} ${GSTREAMER_LIBRARY} ${GSTREAMER_VIDEO_LIBRARY}
                          ${GSTREAMER_AUDIO_LIBRARY} ${GSTREAMER_PBUTILS_LIBRARY}
                          ${GSTREAMER_APP_LIBRARY})
  set(GSTREAMER_INCLUDE_DIRS ${PC_GSTREAMER_INCLUDE_DIR})
  if(PC_GSTREAMER_INCLUDE_DIRS)
    list(APPEND GSTREAMER_INCLUDE_DIRS ${PC_GSTREAMER_INCLUDE_DIRS})
  endif()
  # Make it available to our compiler
  add_definitions(-DGSTREAMER_FOUND)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GStreamer
                                  REQUIRED_VARS GSTREAMER_LIBRARIES
                                                GSTREAMER_INCLUDE_DIRS)

mark_as_advanced(GSTLIBS_INCLUDE_DIR GSTLIBS_LIBRARIES)
