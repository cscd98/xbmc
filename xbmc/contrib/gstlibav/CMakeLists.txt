if(GSTREAMER_FOUND)
  set(SOURCES gstavcodecmap.c)

  set(HEADERS gstavcodecmap.h)

  core_add_library(gstlibav)
endif()
