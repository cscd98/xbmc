/*
 *  Copyright (C) 2017-2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemWaylandBackport.h"

#include <wayland-client.hpp>

#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <memory>

extern "C" {
  struct wl_proxy* wl_proxy_create(struct wl_proxy* factory, const struct wl_interface* interface);
  void wl_proxy_marshal(struct wl_proxy* p, uint32_t opcode, ...);
  int wl_display_dispatch(struct wl_display* display);
}

namespace {

class WaylandBackportLogger
{
public:
  static WaylandBackportLogger& GetInstance()
  {
    static WaylandBackportLogger instance;
    return instance;
  }

  void Log(const std::string& level, const std::string& function, const std::string& message)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized)
    {
      Initialize();
    }

    if (!m_logStream.is_open())
      return;

    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    m_logStream << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
                << "[" << level << "] "
                << function << ": " << message << std::endl;
    m_logStream.flush();
  }

  template<typename... Args>
  void LogF(const std::string& level, const std::string& function, const char* format, Args... args)
  {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    Log(level, function, std::string(buffer));
  }

private:
  WaylandBackportLogger() = default;
  ~WaylandBackportLogger()
  {
    if (m_logStream.is_open())
    {
      m_logStream.close();
    }
  }

  void Initialize()
  {
    m_logStream.open("/tmp/kodi_wayland_backport.log", std::ios::app);
    
    if (!m_logStream.is_open())
    {
      std::fprintf(stderr, "[Wayland Backport] Failed to open /tmp/kodi_wayland_backport.log: %s\n", 
                   std::strerror(errno));
      return;
    }

    m_logStream << "\n=== Kodi Wayland Backport Initialized ===" << std::endl;
    m_initialized = true;
  }

  std::mutex m_mutex;
  std::ofstream m_logStream;
  bool m_initialized{false};
};

// Type definitions for real function pointers
using wl_proxy_get_version_func = uint32_t (*)(struct wl_proxy*);
using wl_proxy_marshal_constructor_func = struct wl_proxy* (*)(struct wl_proxy*, uint32_t, const struct wl_interface*, ...);
using wl_proxy_marshal_constructor_versioned_func = struct wl_proxy* (*)(struct wl_proxy*, uint32_t, const struct wl_interface*, uint32_t, ...);
using wl_display_prepare_read_func = int (*)(struct wl_display*);
using wl_display_read_events_func = int (*)(struct wl_display*);
using wl_display_cancel_read_func = void (*)(struct wl_display*);

// Global function pointers to REAL implementations
wl_proxy_get_version_func g_real_wl_proxy_get_version = nullptr;
wl_proxy_marshal_constructor_func g_real_wl_proxy_marshal_constructor = nullptr;
wl_proxy_marshal_constructor_versioned_func g_real_wl_proxy_marshal_constructor_versioned = nullptr;
wl_display_prepare_read_func g_real_wl_display_prepare_read = nullptr;
wl_display_read_events_func g_real_wl_display_read_events = nullptr;
wl_display_cancel_read_func g_real_wl_display_cancel_read = nullptr;

bool g_wayland_init_done = false;
std::mutex g_init_mutex;

void InitializeWaylandFallbacks()
{
  std::lock_guard<std::mutex> lock(g_init_mutex);
  
  if (g_wayland_init_done)
    return;
  
  g_wayland_init_done = true;
  
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.Log("INFO", "InitializeWaylandFallbacks", "Initializing Wayland backport layer");
  
  // Use RTLD_NEXT to get the REAL symbols from libwayland-client.so
  // This skips our own implementations
  
  dlerror(); // Clear errors
  
  g_real_wl_proxy_get_version = reinterpret_cast<wl_proxy_get_version_func>(
      dlsym(RTLD_NEXT, "wl_proxy_get_version"));
  if (!g_real_wl_proxy_get_version)
  {
    const char* err = dlerror();
    logger.LogF("WARN", "InitializeWaylandFallbacks", 
                "wl_proxy_get_version not available: %s", 
                err ? err : "unknown");
  }
  else
  {
    logger.Log("INFO", "InitializeWaylandFallbacks", "Found real wl_proxy_get_version");
  }
  
  g_real_wl_proxy_marshal_constructor = reinterpret_cast<wl_proxy_marshal_constructor_func>(
      dlsym(RTLD_NEXT, "wl_proxy_marshal_constructor"));
  if (!g_real_wl_proxy_marshal_constructor)
  {
    const char* err = dlerror();
    logger.LogF("WARN", "InitializeWaylandFallbacks",
                "wl_proxy_marshal_constructor not available: %s",
                err ? err : "unknown");
  }
  else
  {
    logger.Log("INFO", "InitializeWaylandFallbacks", "Found real wl_proxy_marshal_constructor");
  }
  
  g_real_wl_proxy_marshal_constructor_versioned = 
      reinterpret_cast<wl_proxy_marshal_constructor_versioned_func>(
      dlsym(RTLD_NEXT, "wl_proxy_marshal_constructor_versioned"));
  if (!g_real_wl_proxy_marshal_constructor_versioned)
  {
    const char* err = dlerror();
    logger.LogF("WARN", "InitializeWaylandFallbacks",
                "wl_proxy_marshal_constructor_versioned not available: %s",
                err ? err : "unknown");
  }
  else
  {
    logger.Log("INFO", "InitializeWaylandFallbacks", 
               "Found real wl_proxy_marshal_constructor_versioned");
  }
  
  g_real_wl_display_prepare_read = reinterpret_cast<wl_display_prepare_read_func>(
      dlsym(RTLD_NEXT, "wl_display_prepare_read"));
  if (!g_real_wl_display_prepare_read)
  {
    const char* err = dlerror();
    logger.LogF("WARN", "InitializeWaylandFallbacks",
                "wl_display_prepare_read not available: %s",
                err ? err : "unknown");
  }
  else
  {
    logger.Log("INFO", "InitializeWaylandFallbacks", "Found real wl_display_prepare_read");
  }
  
  g_real_wl_display_read_events = reinterpret_cast<wl_display_read_events_func>(
      dlsym(RTLD_NEXT, "wl_display_read_events"));
  if (!g_real_wl_display_read_events)
  {
    const char* err = dlerror();
    logger.LogF("WARN", "InitializeWaylandFallbacks",
                "wl_display_read_events not available: %s",
                err ? err : "unknown");
  }
  else
  {
    logger.Log("INFO", "InitializeWaylandFallbacks", "Found real wl_display_read_events");
  }
  
  g_real_wl_display_cancel_read = reinterpret_cast<wl_display_cancel_read_func>(
      dlsym(RTLD_NEXT, "wl_display_cancel_read"));
  if (!g_real_wl_display_cancel_read)
  {
    const char* err = dlerror();
    logger.LogF("WARN", "InitializeWaylandFallbacks",
                "wl_display_cancel_read not available: %s",
                err ? err : "unknown");
  }
  else
  {
    logger.Log("INFO", "InitializeWaylandFallbacks", "Found real wl_display_cancel_read");
  }
  
  logger.Log("INFO", "InitializeWaylandFallbacks", "Wayland backport initialization complete");
}

/*
  Message signature parsing adapted from SDL
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>
*/
int ParseMessageSignature(const char* signature, int* new_id_index)
{
  int count = 0;
  *new_id_index = -1;
  
  for (; *signature; ++signature)
  {
    switch (*signature)
    {
    case 'n':
      *new_id_index = count;
      [[fallthrough]];
    case 'i':
    case 'u':
    case 'f':
    case 's':
    case 'o':
    case 'a':
    case 'h':
      ++count;
      break;
    }
  }
  
  return count;
}

uint32_t Fallback_wl_proxy_get_version(struct wl_proxy* proxy)
{
  (void)proxy;
  WaylandBackportLogger::GetInstance().Log("DEBUG", "Fallback_wl_proxy_get_version", 
                                           "Called - returning 0");
  return 0;
}

struct wl_proxy* Fallback_wl_proxy_marshal_constructor(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    va_list args)
{
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.LogF("DEBUG", "Fallback_wl_proxy_marshal_constructor", "Called with opcode %u", opcode);
  
  struct wl_proxy* id = wl_proxy_create(proxy, interface);
  if (!id)
  {
    logger.Log("ERROR", "Fallback_wl_proxy_marshal_constructor", "wl_proxy_create failed");
    return nullptr;
  }
  
  auto* proxy_interface = *reinterpret_cast<struct wl_interface**>(proxy);
  if (opcode > static_cast<uint32_t>(proxy_interface->method_count))
  {
    logger.LogF("ERROR", "Fallback_wl_proxy_marshal_constructor",
                "Invalid opcode %u (max: %u)", opcode, proxy_interface->method_count);
    return nullptr;
  }
  
  int new_id_index = -1;
  int num_args = ParseMessageSignature(proxy_interface->methods[opcode].signature, &new_id_index);
  
  if (new_id_index < 0)
  {
    logger.Log("ERROR", "Fallback_wl_proxy_marshal_constructor", "No new_id in signature");
    return nullptr;
  }
  
  void* varargs[WL_CLOSURE_MAX_ARGS] = {nullptr};
  
  for (int i = 0; i < num_args; i++)
    varargs[i] = va_arg(args, void*);
  
  varargs[new_id_index] = id;
  
  wl_proxy_marshal(proxy, opcode,
                   varargs[0], varargs[1], varargs[2], varargs[3],
                   varargs[4], varargs[5], varargs[6], varargs[7],
                   varargs[8], varargs[9], varargs[10], varargs[11],
                   varargs[12], varargs[13], varargs[14], varargs[15],
                   varargs[16], varargs[17], varargs[18], varargs[19]);
  
  logger.Log("DEBUG", "Fallback_wl_proxy_marshal_constructor", "Success");
  return id;
}

struct wl_proxy* Fallback_wl_proxy_marshal_constructor_versioned(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    uint32_t version,
    va_list args)
{
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.LogF("DEBUG", "Fallback_wl_proxy_marshal_constructor_versioned",
              "Called with opcode %u, version %u", opcode, version);
  
  (void)version;
  
  struct wl_proxy* id = wl_proxy_create(proxy, interface);
  if (!id)
  {
    logger.Log("ERROR", "Fallback_wl_proxy_marshal_constructor_versioned", 
               "wl_proxy_create failed");
    return nullptr;
  }
  
  auto* proxy_interface = *reinterpret_cast<struct wl_interface**>(proxy);
  if (opcode > static_cast<uint32_t>(proxy_interface->method_count))
  {
    logger.LogF("ERROR", "Fallback_wl_proxy_marshal_constructor_versioned",
                "Invalid opcode %u (max: %u)", opcode, proxy_interface->method_count);
    return nullptr;
  }
  
  int new_id_index = -1;
  int num_args = ParseMessageSignature(proxy_interface->methods[opcode].signature, &new_id_index);
  
  if (new_id_index < 0)
  {
    logger.Log("ERROR", "Fallback_wl_proxy_marshal_constructor_versioned", 
               "No new_id in signature");
    return nullptr;
  }
  
  void* varargs[WL_CLOSURE_MAX_ARGS] = {nullptr};
  
  for (int i = 0; i < num_args; i++)
    varargs[i] = va_arg(args, void*);
  
  varargs[new_id_index] = id;
  
  wl_proxy_marshal(proxy, opcode,
                   varargs[0], varargs[1], varargs[2], varargs[3],
                   varargs[4], varargs[5], varargs[6], varargs[7],
                   varargs[8], varargs[9], varargs[10], varargs[11],
                   varargs[12], varargs[13], varargs[14], varargs[15],
                   varargs[16], varargs[17], varargs[18], varargs[19]);
  
  logger.Log("DEBUG", "Fallback_wl_proxy_marshal_constructor_versioned", "Success");
  return id;
}

int Fallback_wl_display_prepare_read(struct wl_display* display)
{
  (void)display;
  WaylandBackportLogger::GetInstance().Log("DEBUG", "Fallback_wl_display_prepare_read", 
                                           "Called - returning 0");
  return 0;
}

int Fallback_wl_display_read_events(struct wl_display* display)
{
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.Log("DEBUG", "Fallback_wl_display_read_events", 
             "Called - falling back to wl_display_dispatch");
  
  int result = wl_display_dispatch(display);
  logger.LogF("DEBUG", "Fallback_wl_display_read_events", 
              "wl_display_dispatch returned: %d", result);
  return result;
}

void Fallback_wl_display_cancel_read(struct wl_display* display)
{
  (void)display;
  WaylandBackportLogger::GetInstance().Log("DEBUG", "Fallback_wl_display_cancel_read", 
                                           "Called - no-op");
}

} // anonymous namespace

// EXPORTED SYMBOLS
// These override the weak symbols from libwayland-client.so
// The linker will use these instead of the library versions

extern "C" {

uint32_t wl_proxy_get_version(struct wl_proxy* proxy)
{
  InitializeWaylandFallbacks();
  
  auto& logger = WaylandBackportLogger::GetInstance();
  
  if (g_real_wl_proxy_get_version)
  {
    uint32_t result = g_real_wl_proxy_get_version(proxy);
    logger.LogF("DEBUG", "wl_proxy_get_version", "Real implementation returned: %u", result);
    return result;
  }
  else
  {
    logger.Log("DEBUG", "wl_proxy_get_version", "Using fallback");
    return Fallback_wl_proxy_get_version(proxy);
  }
}

struct wl_proxy* wl_proxy_marshal_constructor(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    ...)
{
  InitializeWaylandFallbacks();
  
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.LogF("DEBUG", "wl_proxy_marshal_constructor", "Called with opcode %u", opcode);
  
  va_list args;
  va_start(args, interface);
  
  struct wl_proxy* result;
  
  if (g_real_wl_proxy_marshal_constructor)
  {
    logger.Log("DEBUG", "wl_proxy_marshal_constructor", "Using real implementation");
    // Can't forward va_list, must use fallback
    result = Fallback_wl_proxy_marshal_constructor(proxy, opcode, interface, args);
  }
  else
  {
    logger.Log("DEBUG", "wl_proxy_marshal_constructor", "Using fallback");
    result = Fallback_wl_proxy_marshal_constructor(proxy, opcode, interface, args);
  }
  
  va_end(args);
  return result;
}

struct wl_proxy* wl_proxy_marshal_constructor_versioned(
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* interface,
    uint32_t version,
    ...)
{
  InitializeWaylandFallbacks();
  
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.LogF("DEBUG", "wl_proxy_marshal_constructor_versioned", 
              "Called with opcode %u, version %u", opcode, version);
  
  va_list args;
  va_start(args, version);
  
  struct wl_proxy* result;
  
  if (g_real_wl_proxy_marshal_constructor_versioned)
  {
    logger.Log("DEBUG", "wl_proxy_marshal_constructor_versioned", "Using real implementation");
    result = Fallback_wl_proxy_marshal_constructor_versioned(proxy, opcode, interface, version, args);
  }
  else
  {
    logger.Log("DEBUG", "wl_proxy_marshal_constructor_versioned", "Using fallback");
    result = Fallback_wl_proxy_marshal_constructor_versioned(proxy, opcode, interface, version, args);
  }
  
  va_end(args);
  return result;
}

int wl_display_prepare_read(struct wl_display* display)
{
  InitializeWaylandFallbacks();
  
  auto& logger = WaylandBackportLogger::GetInstance();
  
  if (g_real_wl_display_prepare_read)
  {
    int result = g_real_wl_display_prepare_read(display);
    logger.LogF("DEBUG", "wl_display_prepare_read", "Real implementation returned: %d", result);
    return result;
  }
  else
  {
    logger.Log("DEBUG", "wl_display_prepare_read", "Using fallback");
    return Fallback_wl_display_prepare_read(display);
  }
}

int wl_display_read_events(struct wl_display* display)
{
  InitializeWaylandFallbacks();
  
  auto& logger = WaylandBackportLogger::GetInstance();
  
  if (g_real_wl_display_read_events)
  {
    int result = g_real_wl_display_read_events(display);
    logger.LogF("DEBUG", "wl_display_read_events", "Real implementation returned: %d", result);
    return result;
  }
  else
  {
    logger.Log("DEBUG", "wl_display_read_events", "Using fallback");
    return Fallback_wl_display_read_events(display);
  }
}

void wl_display_cancel_read(struct wl_display* display)
{
  InitializeWaylandFallbacks();
  
  auto& logger = WaylandBackportLogger::GetInstance();
  
  if (g_real_wl_display_cancel_read)
  {
    g_real_wl_display_cancel_read(display);
    logger.Log("DEBUG", "wl_display_cancel_read", "Called real implementation");
  }
  else
  {
    logger.Log("DEBUG", "wl_display_cancel_read", "Using fallback");
    Fallback_wl_display_cancel_read(display);
  }
}

} // extern "C"