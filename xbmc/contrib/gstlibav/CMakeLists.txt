if(GSTREAMER_INCLUDE_DIRS)
  set(SOURCES git/gstav.c
              git/gstavcfg.c
              git/gstavcodecmap.c
              git/gstavutils.c
              git/gstavvidenc.c)
  set(HEADERS git/gstav.h
              git/gstavcfg.h
              git/gstavcodecmap.h
              git/gstavutils.h
              git/gstavvidenc.h)
  #set(SOURCES gstavcodecmap.c)
  #set(HEADERS gstavcodecmap.h)
  core_add_library(gstlibav)
endif()
