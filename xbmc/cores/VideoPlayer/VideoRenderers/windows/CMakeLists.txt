set(SOURCES RendererBase.cpp
            RendererDXVA.cpp
            RendererHQ.cpp
            RendererShaders.cpp
            RendererSoftware.cpp
            RendererMF.cpp)

set(HEADERS RendererBase.h
            RendererDXVA.h
            RendererHQ.h
            RendererShaders.h
            RendererSoftware.h
            RendererMF.h)

if(CORE_SYSTEM_NAME STREQUAL windowsstore)
  list(APPEND SOURCES DolbyVisionEffect.cpp)
  list(APPEND HEADERS DolbyVisionEffect.h)
endif()

core_add_library(videorenderers-windows)
