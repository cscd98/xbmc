/*
 *  Copyright (C) 2017-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AlsaBackport.h"

#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <mutex>
#include <fstream>
#include <string>
#include <ctime>
#include <iomanip>

static bool enable_alsa_logging = true;

namespace {

class AlsaBackportLogger
{
public:
  static AlsaBackportLogger& GetInstance()
  {
    static AlsaBackportLogger instance;
    return instance;
  }

  void Log(const std::string& level, const std::string& function, const std::string& message)
  {
    if (!enable_alsa_logging)
      return;

    if (message.empty())
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
                << "[ALSA] [" << level << "] "
                << function << ": " << message << std::endl;
    m_logStream.flush();
  }

  template<typename... Args>
  void LogF(const std::string& level, const std::string& function, const char* format, Args... args)
  {
    if (!enable_alsa_logging)
      return;

    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    Log(level, function, std::string(buffer));
  }

private:
  AlsaBackportLogger() = default;
  ~AlsaBackportLogger()
  {
    if (enable_alsa_logging && m_logStream.is_open())
    {
      m_logStream.close();
    }
  }

  void Initialize()
  {
    if(enable_alsa_logging)
    {
      m_logStream.open("/tmp/kodi.log", std::ios::app);

      if (!m_logStream.is_open())
      {
        std::fprintf(stderr, "[ALSA Backport] Failed to open /tmp/kodi.log\n");
        return;
      }

      m_logStream << "\n=== Kodi ALSA Backport Initialized ===" << std::endl;
    }

    m_initialized = true;
  }

  std::mutex m_mutex;
  std::ofstream m_logStream;
  bool m_initialized{false};
};

// Function pointer types
using snd_pcm_set_chmap_func = int (*)(snd_pcm_t*, const snd_pcm_chmap_t*);
using snd_pcm_query_chmaps_func = snd_pcm_chmap_query_t** (*)(snd_pcm_t*);
using snd_pcm_get_chmap_func = snd_pcm_chmap_t* (*)(snd_pcm_t*);
using snd_pcm_free_chmaps_func = void (*)(snd_pcm_chmap_query_t**);
using snd_pcm_chmap_print_func = int (*)(const snd_pcm_chmap_t*, size_t, char*);

// Global pointers to real implementations
snd_pcm_set_chmap_func g_real_snd_pcm_set_chmap = nullptr;
snd_pcm_query_chmaps_func g_real_snd_pcm_query_chmaps = nullptr;
snd_pcm_get_chmap_func g_real_snd_pcm_get_chmap = nullptr;
snd_pcm_free_chmaps_func g_real_snd_pcm_free_chmaps = nullptr;
snd_pcm_chmap_print_func g_real_snd_pcm_chmap_print = nullptr;

bool g_alsa_init_done = false;
std::mutex g_alsa_init_mutex;

void InitializeAlsaFallbacks()
{
  std::lock_guard<std::mutex> lock(g_alsa_init_mutex);
  
  if (g_alsa_init_done)
    return;
  
  g_alsa_init_done = true;
  
  auto& logger = AlsaBackportLogger::GetInstance();
  logger.Log("INFO", "InitializeAlsaFallbacks", "Initializing ALSA backport layer");
  
  dlerror();
  
  g_real_snd_pcm_set_chmap = reinterpret_cast<snd_pcm_set_chmap_func>(
      dlsym(RTLD_NEXT, "snd_pcm_set_chmap"));
  logger.Log(g_real_snd_pcm_set_chmap ? "INFO" : "WARN", "InitializeAlsaFallbacks",
             g_real_snd_pcm_set_chmap ? "Found real snd_pcm_set_chmap" :
             "snd_pcm_set_chmap not available - using fallback");
  
  g_real_snd_pcm_query_chmaps = reinterpret_cast<snd_pcm_query_chmaps_func>(
      dlsym(RTLD_NEXT, "snd_pcm_query_chmaps"));
  logger.Log(g_real_snd_pcm_query_chmaps ? "INFO" : "WARN", "InitializeAlsaFallbacks",
             g_real_snd_pcm_query_chmaps ? "Found real snd_pcm_query_chmaps" :
             "snd_pcm_query_chmaps not available - using fallback");
  
  g_real_snd_pcm_get_chmap = reinterpret_cast<snd_pcm_get_chmap_func>(
      dlsym(RTLD_NEXT, "snd_pcm_get_chmap"));
  logger.Log(g_real_snd_pcm_get_chmap ? "INFO" : "WARN", "InitializeAlsaFallbacks",
             g_real_snd_pcm_get_chmap ? "Found real snd_pcm_get_chmap" :
             "snd_pcm_get_chmap not available - using fallback");
  
  g_real_snd_pcm_free_chmaps = reinterpret_cast<snd_pcm_free_chmaps_func>(
      dlsym(RTLD_NEXT, "snd_pcm_free_chmaps"));
  logger.Log(g_real_snd_pcm_free_chmaps ? "INFO" : "WARN", "InitializeAlsaFallbacks",
             g_real_snd_pcm_free_chmaps ? "Found real snd_pcm_free_chmaps" :
             "snd_pcm_free_chmaps not available - using fallback");
  
  g_real_snd_pcm_chmap_print = reinterpret_cast<snd_pcm_chmap_print_func>(
      dlsym(RTLD_NEXT, "snd_pcm_chmap_print"));
  logger.Log(g_real_snd_pcm_chmap_print ? "INFO" : "WARN", "InitializeAlsaFallbacks",
             g_real_snd_pcm_chmap_print ? "Found real snd_pcm_chmap_print" :
             "snd_pcm_chmap_print not available - using fallback");
  
  logger.Log("INFO", "InitializeAlsaFallbacks", "ALSA backport initialization complete");
}

// Fallback implementations

int Fallback_snd_pcm_set_chmap(snd_pcm_t* pcm, const snd_pcm_chmap_t* map)
{
  auto& logger = AlsaBackportLogger::GetInstance();
  logger.Log("WARN", "Fallback_snd_pcm_set_chmap",
             "Channel map setting not supported - ignoring (old ALSA)");
  (void)pcm;
  (void)map;
  // Return success - audio will work but without custom channel mapping
  return 0;
}

snd_pcm_chmap_query_t** Fallback_snd_pcm_query_chmaps(snd_pcm_t* pcm)
{
  auto& logger = AlsaBackportLogger::GetInstance();
  logger.Log("WARN", "Fallback_snd_pcm_query_chmaps",
             "Channel map query not supported - returning NULL (old ALSA)");
  (void)pcm;
  // Return NULL to indicate no channel maps available
  // Kodi will fall back to default channel configuration
  return nullptr;
}

snd_pcm_chmap_t* Fallback_snd_pcm_get_chmap(snd_pcm_t* pcm)
{
  auto& logger = AlsaBackportLogger::GetInstance();
  logger.Log("WARN", "Fallback_snd_pcm_get_chmap",
             "Channel map get not supported - returning NULL (old ALSA)");
  (void)pcm;
  // Return NULL to indicate channel map not available
  // Kodi will use default channel mapping
  return nullptr;
}

void Fallback_snd_pcm_free_chmaps(snd_pcm_chmap_query_t** maps)
{
  auto& logger = AlsaBackportLogger::GetInstance();
  (void)maps;
  // No-op since our fallback query returns NULL anyway
  logger.Log("DEBUG", "Fallback_snd_pcm_free_chmaps", "Called (no-op)");
}

int Fallback_snd_pcm_chmap_print(const snd_pcm_chmap_t* map, size_t maxlen, char* buf)
{
  auto& logger = AlsaBackportLogger::GetInstance();
  logger.Log("WARN", "Fallback_snd_pcm_chmap_print",
             "Channel map print not supported (old ALSA)");
  
  if (buf && maxlen > 0)
  {
    // Return a placeholder string
    std::snprintf(buf, maxlen, "unknown");
    return 7; // length of "unknown"
  }
  
  (void)map;
  return 0;
}

} // anonymous namespace

// Exported symbols - override ALSA library functions

extern "C" {

int snd_pcm_set_chmap(snd_pcm_t* pcm, const snd_pcm_chmap_t* map)
{
  InitializeAlsaFallbacks();
  return g_real_snd_pcm_set_chmap ? g_real_snd_pcm_set_chmap(pcm, map) :
                                    Fallback_snd_pcm_set_chmap(pcm, map);
}

snd_pcm_chmap_query_t** snd_pcm_query_chmaps(snd_pcm_t* pcm)
{
  InitializeAlsaFallbacks();
  return g_real_snd_pcm_query_chmaps ? g_real_snd_pcm_query_chmaps(pcm) :
                                       Fallback_snd_pcm_query_chmaps(pcm);
}

snd_pcm_chmap_t* snd_pcm_get_chmap(snd_pcm_t* pcm)
{
  InitializeAlsaFallbacks();
  return g_real_snd_pcm_get_chmap ? g_real_snd_pcm_get_chmap(pcm) :
                                    Fallback_snd_pcm_get_chmap(pcm);
}

void snd_pcm_free_chmaps(snd_pcm_chmap_query_t** maps)
{
  InitializeAlsaFallbacks();
  if (g_real_snd_pcm_free_chmaps)
    g_real_snd_pcm_free_chmaps(maps);
  else
    Fallback_snd_pcm_free_chmaps(maps);
}

int snd_pcm_chmap_print(const snd_pcm_chmap_t* map, size_t maxlen, char* buf)
{
  InitializeAlsaFallbacks();
  return g_real_snd_pcm_chmap_print ? g_real_snd_pcm_chmap_print(map, maxlen, buf) :
                                      Fallback_snd_pcm_chmap_print(map, maxlen, buf);
}

} // extern "C"
