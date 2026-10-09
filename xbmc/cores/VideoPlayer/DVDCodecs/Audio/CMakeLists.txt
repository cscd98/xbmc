set(SOURCES DVDAudioCodecFFmpeg.cpp
            DVDAudioCodecPassthrough.cpp)

set(HEADERS DVDAudioCodec.h
            DVDAudioCodecFFmpeg.h
            DVDAudioCodecPassthrough.h)

if(CORE_SYSTEM_NAME STREQUAL android)
  list(APPEND SOURCES DVDAudioCodecAndroidMediaCodec.cpp)
  list(APPEND HEADERS DVDAudioCodecAndroidMediaCodec.h)
endif()

if(CORE_SYSTEM_NAME STREQUAL windowsstore)
  list(APPEND SOURCES DVDAudioCodecMF.cpp)
  list(APPEND HEADERS DVDAudioCodecMF.h)
endif()

core_add_library(dvdaudiocodecs)
