/*
 *  Copyright (C) 2017-2024 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemWaylandBackport.h"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

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

static bool enable_logging = true;

extern "C" {
  struct wl_proxy* wl_proxy_create(struct wl_proxy* factory, const struct wl_interface* interface);
  void wl_proxy_marshal(struct wl_proxy* p, uint32_t opcode, ...);
  int wl_display_dispatch(struct wl_display* display);
  int wl_display_roundtrip(struct wl_display* display);
  void wl_proxy_destroy(struct wl_proxy* proxy);
  void wl_proxy_set_user_data(struct wl_proxy* proxy, void* user_data);
  void* wl_proxy_get_user_data(struct wl_proxy* proxy);
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
    if (!enable_logging)
      return;

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
    if (!enable_logging)
      return;

    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    Log(level, function, std::string(buffer));
  }

private:
  WaylandBackportLogger() = default;
  ~WaylandBackportLogger()
  {
    if (enable_logging && m_logStream.is_open())
    {
      m_logStream.close();
    }
  }

  void Initialize()
  {
    if(enable_logging)
    {
      m_logStream.open("/tmp/kodi.log", std::ios::out | std::ios::trunc);

      if (!m_logStream.is_open())
      {
        std::fprintf(stderr, "[Wayland Backport] Failed to open /tmp/kodi.log: %s\n",
                    std::strerror(errno));
        return;
      }

      m_logStream << "\n=== Kodi Wayland Backport Initialized ===" << std::endl;
    }

    m_initialized = true;
  }

  std::mutex m_mutex;
  std::ofstream m_logStream;
  bool m_initialized{false};
};

using wl_proxy_get_version_func = uint32_t (*)(struct wl_proxy*);
using wl_proxy_marshal_constructor_func = struct wl_proxy* (*)(struct wl_proxy*, uint32_t, const struct wl_interface*, ...);
using wl_proxy_marshal_constructor_versioned_func = struct wl_proxy* (*)(struct wl_proxy*, uint32_t, const struct wl_interface*, uint32_t, ...);
using wl_display_prepare_read_func = int (*)(struct wl_display*);
using wl_display_read_events_func = int (*)(struct wl_display*);
using wl_display_cancel_read_func = void (*)(struct wl_display*);
using wl_proxy_create_wrapper_func = void* (*)(void*);
using wl_display_prepare_read_queue_func = int (*)(struct wl_display*, struct wl_event_queue*);
using wl_display_roundtrip_queue_func = int (*)(struct wl_display*, struct wl_event_queue*);
using wl_proxy_marshal_array_func = void (*)(struct wl_proxy*, uint32_t, union wl_argument*);
using wl_proxy_marshal_array_constructor_func = struct wl_proxy* (*)(struct wl_proxy*, uint32_t, union wl_argument*, const struct wl_interface*);
using wl_proxy_marshal_array_constructor_versioned_func = struct wl_proxy* (*)(struct wl_proxy*, uint32_t, union wl_argument*, const struct wl_interface*, uint32_t);
using wl_proxy_add_dispatcher_func = int (*)(struct wl_proxy*, wl_dispatcher_func_t, const void*, void*);
using wl_proxy_wrapper_destroy_func = void* (*)(void*);
using wl_proxy_get_class_func = const char* (*)(struct wl_proxy*);

wl_proxy_get_version_func g_real_wl_proxy_get_version = nullptr;
wl_proxy_marshal_constructor_func g_real_wl_proxy_marshal_constructor = nullptr;
wl_proxy_marshal_constructor_versioned_func g_real_wl_proxy_marshal_constructor_versioned = nullptr;
wl_display_prepare_read_func g_real_wl_display_prepare_read = nullptr;
wl_display_read_events_func g_real_wl_display_read_events = nullptr;
wl_display_cancel_read_func g_real_wl_display_cancel_read = nullptr;
wl_proxy_create_wrapper_func g_real_wl_proxy_create_wrapper = nullptr;
wl_display_prepare_read_queue_func g_real_wl_display_prepare_read_queue = nullptr;
wl_display_roundtrip_queue_func g_real_wl_display_roundtrip_queue = nullptr;
wl_proxy_marshal_array_func g_real_wl_proxy_marshal_array = nullptr;
wl_proxy_marshal_array_constructor_func g_real_wl_proxy_marshal_array_constructor = nullptr;
wl_proxy_marshal_array_constructor_versioned_func g_real_wl_proxy_marshal_array_constructor_versioned = nullptr;
wl_proxy_add_dispatcher_func g_real_wl_proxy_add_dispatcher = nullptr;
wl_proxy_wrapper_destroy_func g_real_wl_proxy_wrapper_destroy = nullptr;
wl_proxy_get_class_func g_real_wl_proxy_get_class = nullptr;

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

  dlerror();
  
  g_real_wl_proxy_get_version = reinterpret_cast<wl_proxy_get_version_func>(dlsym(RTLD_NEXT, "wl_proxy_get_version"));
  logger.Log(g_real_wl_proxy_get_version ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_get_version ? "Found real wl_proxy_get_version" : "wl_proxy_get_version not available - using fallback");

  g_real_wl_proxy_marshal_constructor = reinterpret_cast<wl_proxy_marshal_constructor_func>(dlsym(RTLD_NEXT, "wl_proxy_marshal_constructor"));
  logger.Log(g_real_wl_proxy_marshal_constructor ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_marshal_constructor ? "Found real wl_proxy_marshal_constructor" : "wl_proxy_marshal_constructor not available - using fallback");

  g_real_wl_proxy_marshal_constructor_versioned = reinterpret_cast<wl_proxy_marshal_constructor_versioned_func>(dlsym(RTLD_NEXT, "wl_proxy_marshal_constructor_versioned"));
  logger.Log(g_real_wl_proxy_marshal_constructor_versioned ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_marshal_constructor_versioned ? "Found real wl_proxy_marshal_constructor_versioned" : "wl_proxy_marshal_constructor_versioned not available - using fallback");

  g_real_wl_display_prepare_read = reinterpret_cast<wl_display_prepare_read_func>(dlsym(RTLD_NEXT, "wl_display_prepare_read"));
  logger.Log(g_real_wl_display_prepare_read ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_display_prepare_read ? "Found real wl_display_prepare_read" : "wl_display_prepare_read not available - using fallback");

  g_real_wl_display_read_events = reinterpret_cast<wl_display_read_events_func>(dlsym(RTLD_NEXT, "wl_display_read_events"));
  logger.Log(g_real_wl_display_read_events ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_display_read_events ? "Found real wl_display_read_events" : "wl_display_read_events not available - using fallback");

  g_real_wl_display_cancel_read = reinterpret_cast<wl_display_cancel_read_func>(dlsym(RTLD_NEXT, "wl_display_cancel_read"));
  logger.Log(g_real_wl_display_cancel_read ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_display_cancel_read ? "Found real wl_display_cancel_read" : "wl_display_cancel_read not available - using fallback");

  g_real_wl_proxy_create_wrapper = reinterpret_cast<wl_proxy_create_wrapper_func>(dlsym(RTLD_NEXT, "wl_proxy_create_wrapper"));
  logger.Log(g_real_wl_proxy_create_wrapper ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_create_wrapper ? "Found real wl_proxy_create_wrapper" : "wl_proxy_create_wrapper not available - using fallback");

  g_real_wl_display_prepare_read_queue = reinterpret_cast<wl_display_prepare_read_queue_func>(dlsym(RTLD_NEXT, "wl_display_prepare_read_queue"));
  logger.Log(g_real_wl_display_prepare_read_queue ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_display_prepare_read_queue ? "Found real wl_display_prepare_read_queue" : "wl_display_prepare_read_queue not available - using fallback");

  g_real_wl_display_roundtrip_queue = reinterpret_cast<wl_display_roundtrip_queue_func>(dlsym(RTLD_NEXT, "wl_display_roundtrip_queue"));
  logger.Log(g_real_wl_display_roundtrip_queue ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_display_roundtrip_queue ? "Found real wl_display_roundtrip_queue" : "wl_display_roundtrip_queue not available - using fallback");

  g_real_wl_proxy_marshal_array = reinterpret_cast<wl_proxy_marshal_array_func>(dlsym(RTLD_NEXT, "wl_proxy_marshal_array"));
  logger.Log(g_real_wl_proxy_marshal_array ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_marshal_array ? "Found real wl_proxy_marshal_array" : "wl_proxy_marshal_array not available - using fallback");

  g_real_wl_proxy_marshal_array_constructor = reinterpret_cast<wl_proxy_marshal_array_constructor_func>(dlsym(RTLD_NEXT, "wl_proxy_marshal_array_constructor"));
  logger.Log(g_real_wl_proxy_marshal_array_constructor ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_marshal_array_constructor ? "Found real wl_proxy_marshal_array_constructor" : "wl_proxy_marshal_array_constructor not available - using fallback");

  g_real_wl_proxy_marshal_array_constructor_versioned = reinterpret_cast<wl_proxy_marshal_array_constructor_versioned_func>(dlsym(RTLD_NEXT, "wl_proxy_marshal_array_constructor_versioned"));
  logger.Log(g_real_wl_proxy_marshal_array_constructor_versioned ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_marshal_array_constructor_versioned ? "Found real wl_proxy_marshal_array_constructor_versioned" : "wl_proxy_marshal_array_constructor_versioned not available - using fallback");

  g_real_wl_proxy_add_dispatcher = reinterpret_cast<wl_proxy_add_dispatcher_func>(dlsym(RTLD_NEXT, "wl_proxy_add_dispatcher"));
  logger.Log(g_real_wl_proxy_add_dispatcher ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_add_dispatcher ? "Found real wl_proxy_add_dispatcher" : "wl_proxy_add_dispatcher not available - using fallback");

  g_real_wl_proxy_wrapper_destroy = reinterpret_cast<wl_proxy_wrapper_destroy_func>(dlsym(RTLD_NEXT, "wl_proxy_wrapper_destroy"));
  logger.Log(g_real_wl_proxy_wrapper_destroy ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_wrapper_destroy ? "Found real wl_proxy_wrapper_destroy" : "wl_proxy_wrapper_destroy not available - using fallback");

  g_real_wl_proxy_get_class = reinterpret_cast<wl_proxy_get_class_func>(dlsym(RTLD_NEXT, "wl_proxy_get_class"));
  logger.Log(g_real_wl_proxy_get_class ? "INFO" : "WARN", "InitializeWaylandFallbacks", g_real_wl_proxy_get_class ? "Found real wl_proxy_get_class" : "wl_proxy_get_class not available - using fallback");

  logger.Log("INFO", "InitializeWaylandFallbacks", "Wayland backport initialization complete");
}

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
  return 0;
}

struct wl_proxy* Fallback_wl_proxy_marshal_constructor(struct wl_proxy* proxy, uint32_t opcode, const struct wl_interface* interface, va_list args)
{
  auto& logger = WaylandBackportLogger::GetInstance();

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
    logger.LogF("ERROR", "Fallback_wl_proxy_marshal_constructor",
                "No new_id in signature for opcode %u", opcode);
    return nullptr;
  }

  logger.LogF("DEBUG", "Fallback_wl_proxy_marshal_constructor",
              "opcode=%u, interface=%s, num_args=%d, new_id_index=%d",
              opcode, interface ? interface->name : "null", num_args, new_id_index);

  void* varargs[WL_CLOSURE_MAX_ARGS] = {nullptr};
  for (int i = 0; i < num_args; i++)
    varargs[i] = va_arg(args, void*);
  varargs[new_id_index] = id;

  wl_proxy_marshal(proxy, opcode, varargs[0], varargs[1], varargs[2], varargs[3], varargs[4], varargs[5], varargs[6], varargs[7], varargs[8], varargs[9], varargs[10], varargs[11], varargs[12], varargs[13], varargs[14], varargs[15], varargs[16], varargs[17], varargs[18], varargs[19]);

  logger.Log("DEBUG", "Fallback_wl_proxy_marshal_constructor", "Success");
  return id;
}

struct wl_proxy* Fallback_wl_proxy_marshal_constructor_versioned(struct wl_proxy* proxy, uint32_t opcode, const struct wl_interface* interface, uint32_t version, va_list args)
{
  (void)version;
  return Fallback_wl_proxy_marshal_constructor(proxy, opcode, interface, args);
}

int Fallback_wl_display_prepare_read(struct wl_display* display)
{
  // In old libwayland without prepare_read, we need to check if there are
  // already events in the queue. If so, return -1 to indicate we should
  // dispatch those instead of reading from the socket.
  (void)display;
  // Always return 0 (success) - let read_events handle the actual reading
  return 0;
}

int Fallback_wl_display_read_events(struct wl_display* display)
{
  // DON'T use wl_display_dispatch() here - it will block!
  // Instead, just return 0 to indicate success.
  // The old wayland code will handle event processing differently.
  (void)display;
  return 0;
}

void Fallback_wl_display_cancel_read(struct wl_display* display)
{
  // No-op is correct for old wayland
  (void)display;
}

void *Fallback_wl_proxy_create_wrapper(void *proxy)
{
  auto& logger = WaylandBackportLogger::GetInstance();

  if (!proxy)
  {
    logger.Log("ERROR", "Fallback_wl_proxy_create_wrapper", "NULL proxy passed - returning NULL");
    return nullptr;
  }

  // Wrapper proxies are used for thread-safe event queue handling
  // On old webOS, event queues aren't supported, so we can't create true wrappers
  // However, we MUST return a non-null value or wayland++ will throw
  // The safest fallback is to return the original proxy
  // This means no thread safety, but it's better than crashing

  logger.Log("WARN", "Fallback_wl_proxy_create_wrapper",
             "Proxy wrappers not supported - returning original proxy (no thread safety)");

  // Return the original proxy cast to void*
  // This will work for single-threaded usage
  return proxy;
}

int Fallback_wl_display_prepare_read_queue(struct wl_display* display, struct wl_event_queue* queue)
{
  (void)queue;
  return g_real_wl_display_prepare_read ? g_real_wl_display_prepare_read(display) : 0;
}

int Fallback_wl_display_roundtrip_queue(struct wl_display* display, struct wl_event_queue* queue)
{
  (void)queue;
  return wl_display_roundtrip(display);
}

void Fallback_wl_proxy_marshal_array(struct wl_proxy* p, uint32_t opcode, union wl_argument* args)
{
  wl_proxy_marshal(p, opcode, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i, args[12].i, args[13].i, args[14].i, args[15].i, args[16].i, args[17].i, args[18].i, args[19].i);
}

struct wl_proxy* Fallback_wl_proxy_marshal_array_constructor(struct wl_proxy* proxy, uint32_t opcode, union wl_argument* args, const struct wl_interface* interface)
{
  struct wl_proxy* id = wl_proxy_create(proxy, interface);
  if (!id) return nullptr;
  
  auto* proxy_interface = *reinterpret_cast<struct wl_interface**>(proxy);
  if (opcode > static_cast<uint32_t>(proxy_interface->method_count)) return nullptr;
  
  int new_id_index = -1;
  ParseMessageSignature(proxy_interface->methods[opcode].signature, &new_id_index);
  
  if (new_id_index >= 0 && new_id_index < WL_CLOSURE_MAX_ARGS)
    args[new_id_index].o = reinterpret_cast<struct wl_object*>(id);
  
  Fallback_wl_proxy_marshal_array(proxy, opcode, args);
  return id;
}

struct wl_proxy* Fallback_wl_proxy_marshal_array_constructor_versioned(struct wl_proxy* proxy, uint32_t opcode, union wl_argument* args, const struct wl_interface* interface, uint32_t version)
{
  (void)version;
  return Fallback_wl_proxy_marshal_array_constructor(proxy, opcode, args, interface);
}

int Fallback_wl_proxy_add_dispatcher(struct wl_proxy* proxy, wl_dispatcher_func_t dispatcher_func, const void* dispatcher_data, void* data)
{
  auto& logger = WaylandBackportLogger::GetInstance();
  logger.Log("INFO", "Fallback_wl_proxy_add_dispatcher",
             "Emulating dispatcher with user_data (old libwayland workaround)");

  (void)dispatcher_func;
  (void)dispatcher_data;

  // Old libwayland doesn't have dispatchers, but we can emulate basic functionality
  // by storing the data pointer. The dispatcher_func won't be called, but at least
  // the proxy will have the associated data.
  // This is a limited workaround - full dispatcher functionality requires newer libwayland
  wl_proxy_set_user_data(proxy, data);

  // Return success (0) instead of failure (-1) to prevent exceptions
  return 0;
}

void Fallback_wl_proxy_wrapper_destroy(void *proxy_wrapper)
{
  wl_proxy_destroy(static_cast<struct wl_proxy*>(proxy_wrapper));
}

const char* Fallback_wl_proxy_get_class(struct wl_proxy* proxy)
{
  (void)proxy;
  return "unknown";
}

} // anonymous namespace

extern "C" {

uint32_t wl_proxy_get_version(struct wl_proxy* proxy)
{
  InitializeWaylandFallbacks();
  return g_real_wl_proxy_get_version ? g_real_wl_proxy_get_version(proxy) : Fallback_wl_proxy_get_version(proxy);
}

struct wl_proxy* wl_proxy_marshal_constructor(struct wl_proxy* proxy, uint32_t opcode, const struct wl_interface* interface, ...)
{
  InitializeWaylandFallbacks();
  va_list args;
  va_start(args, interface);
  struct wl_proxy* result;
  if (g_real_wl_proxy_marshal_constructor)
  {
    // Can't forward va_list to real function, have to extract args manually
    // This is a limitation - we use fallback even when real exists
    result = Fallback_wl_proxy_marshal_constructor(proxy, opcode, interface, args);
  }
  else
  {
    result = Fallback_wl_proxy_marshal_constructor(proxy, opcode, interface, args);
  }
  va_end(args);
  return result;
}

struct wl_proxy* wl_proxy_marshal_constructor_versioned(struct wl_proxy* proxy, uint32_t opcode, const struct wl_interface* interface, uint32_t version, ...)
{
  InitializeWaylandFallbacks();
  va_list args;
  va_start(args, version);
  struct wl_proxy* result;
  if (g_real_wl_proxy_marshal_constructor_versioned)
  {
    // Can't forward va_list to real function, have to extract args manually
    // This is a limitation - we use fallback even when real exists
    result = Fallback_wl_proxy_marshal_constructor_versioned(proxy, opcode, interface, version, args);
  }
  else
  {
    result = Fallback_wl_proxy_marshal_constructor_versioned(proxy, opcode, interface, version, args);
  }
  va_end(args);
  return result;
}

int wl_display_prepare_read(struct wl_display* display)
{
  InitializeWaylandFallbacks();
  return g_real_wl_display_prepare_read ? g_real_wl_display_prepare_read(display) : Fallback_wl_display_prepare_read(display);
}

int wl_display_read_events(struct wl_display* display)
{
  InitializeWaylandFallbacks();
  return g_real_wl_display_read_events ? g_real_wl_display_read_events(display) : Fallback_wl_display_read_events(display);
}

void wl_display_cancel_read(struct wl_display* display)
{
  InitializeWaylandFallbacks();
  if (g_real_wl_display_cancel_read) g_real_wl_display_cancel_read(display);
  else Fallback_wl_display_cancel_read(display);
}

void *wl_proxy_create_wrapper(void *proxy)
{
  InitializeWaylandFallbacks();
  return g_real_wl_proxy_create_wrapper ? g_real_wl_proxy_create_wrapper(proxy) : Fallback_wl_proxy_create_wrapper(proxy);
}

int wl_display_prepare_read_queue(struct wl_display* display, struct wl_event_queue* queue)
{
  InitializeWaylandFallbacks();
  return g_real_wl_display_prepare_read_queue ? g_real_wl_display_prepare_read_queue(display, queue) : Fallback_wl_display_prepare_read_queue(display, queue);
}

int wl_display_roundtrip_queue(struct wl_display* display, struct wl_event_queue* queue)
{
  InitializeWaylandFallbacks();
  return g_real_wl_display_roundtrip_queue ? g_real_wl_display_roundtrip_queue(display, queue) : Fallback_wl_display_roundtrip_queue(display, queue);
}

void wl_proxy_marshal_array(struct wl_proxy* p, uint32_t opcode, union wl_argument* args)
{
  InitializeWaylandFallbacks();
  if (g_real_wl_proxy_marshal_array) g_real_wl_proxy_marshal_array(p, opcode, args);
  else Fallback_wl_proxy_marshal_array(p, opcode, args);
}

struct wl_proxy* wl_proxy_marshal_array_constructor(struct wl_proxy* proxy, uint32_t opcode, union wl_argument* args, const struct wl_interface* interface)
{
  InitializeWaylandFallbacks();
  return g_real_wl_proxy_marshal_array_constructor ? g_real_wl_proxy_marshal_array_constructor(proxy, opcode, args, interface) : Fallback_wl_proxy_marshal_array_constructor(proxy, opcode, args, interface);
}

struct wl_proxy* wl_proxy_marshal_array_constructor_versioned(struct wl_proxy* proxy, uint32_t opcode, union wl_argument* args, const struct wl_interface* interface, uint32_t version)
{
  InitializeWaylandFallbacks();
  return g_real_wl_proxy_marshal_array_constructor_versioned ? g_real_wl_proxy_marshal_array_constructor_versioned(proxy, opcode, args, interface, version) : Fallback_wl_proxy_marshal_array_constructor_versioned(proxy, opcode, args, interface, version);
}

int wl_proxy_add_dispatcher(struct wl_proxy* proxy, wl_dispatcher_func_t dispatcher_func, const void* dispatcher_data, void* data)
{
  InitializeWaylandFallbacks();
  return g_real_wl_proxy_add_dispatcher ? g_real_wl_proxy_add_dispatcher(proxy, dispatcher_func, dispatcher_data, data) : Fallback_wl_proxy_add_dispatcher(proxy, dispatcher_func, dispatcher_data, data);
}

void wl_proxy_wrapper_destroy(void *proxy_wrapper)
{
  InitializeWaylandFallbacks();
  if (g_real_wl_proxy_wrapper_destroy) g_real_wl_proxy_wrapper_destroy(proxy_wrapper);
  else Fallback_wl_proxy_wrapper_destroy(proxy_wrapper);
}

const char* wl_proxy_get_class(struct wl_proxy* proxy)
{
  InitializeWaylandFallbacks();
  return g_real_wl_proxy_get_class ? g_real_wl_proxy_get_class(proxy) : Fallback_wl_proxy_get_class(proxy);
}

} // extern "C"
