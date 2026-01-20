/*
 *  Copyright (C) 2017-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AlsaBackport.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <string>

#include <dlfcn.h>

namespace
{

class AlsaBackportLogger
{
public:
  static AlsaBackportLogger& GetInstance()
  {
    static AlsaBackportLogger instance;
    return instance;
  }

private:
  AlsaBackportLogger() = default;
  ~AlsaBackportLogger() {}

  void Initialize() { m_initialized = true; }

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

  g_real_snd_pcm_set_chmap =
      reinterpret_cast<snd_pcm_set_chmap_func>(dlsym(RTLD_NEXT, "snd_pcm_set_chmap"));
  g_real_snd_pcm_query_chmaps =
      reinterpret_cast<snd_pcm_query_chmaps_func>(dlsym(RTLD_NEXT, "snd_pcm_query_chmaps"));
  g_real_snd_pcm_get_chmap =
      reinterpret_cast<snd_pcm_get_chmap_func>(dlsym(RTLD_NEXT, "snd_pcm_get_chmap"));
  g_real_snd_pcm_free_chmaps =
      reinterpret_cast<snd_pcm_free_chmaps_func>(dlsym(RTLD_NEXT, "snd_pcm_free_chmaps"));
  g_real_snd_pcm_chmap_print =
      reinterpret_cast<snd_pcm_chmap_print_func>(dlsym(RTLD_NEXT, "snd_pcm_chmap_print"));
}

// Fallback implementations
int Fallback_snd_pcm_set_chmap(snd_pcm_t* pcm, const snd_pcm_chmap_t* map)
{
  (void)pcm;
  (void)map;

  // Return success
  return 0;
}

snd_pcm_chmap_query_t** Fallback_snd_pcm_query_chmaps(snd_pcm_t* pcm)
{
  (void)pcm;
  // Return NULL to indicate no channel maps available
  return nullptr;
}

snd_pcm_chmap_t* Fallback_snd_pcm_get_chmap(snd_pcm_t* pcm)
{
  (void)pcm;
  // Return NULL to indicate channel map not available
  return nullptr;
}

void Fallback_snd_pcm_free_chmaps(snd_pcm_chmap_query_t** maps)
{
  (void)maps;
}

int Fallback_snd_pcm_chmap_print(const snd_pcm_chmap_t* map, size_t maxlen, char* buf)
{
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
extern "C"
{

  int snd_pcm_set_chmap(snd_pcm_t* pcm, const snd_pcm_chmap_t* map)
  {
    InitializeAlsaFallbacks();
    return g_real_snd_pcm_set_chmap ? g_real_snd_pcm_set_chmap(pcm, map)
                                    : Fallback_snd_pcm_set_chmap(pcm, map);
  }

  snd_pcm_chmap_query_t** snd_pcm_query_chmaps(snd_pcm_t* pcm)
  {
    InitializeAlsaFallbacks();
    return g_real_snd_pcm_query_chmaps ? g_real_snd_pcm_query_chmaps(pcm)
                                       : Fallback_snd_pcm_query_chmaps(pcm);
  }

  snd_pcm_chmap_t* snd_pcm_get_chmap(snd_pcm_t* pcm)
  {
    InitializeAlsaFallbacks();
    return g_real_snd_pcm_get_chmap ? g_real_snd_pcm_get_chmap(pcm)
                                    : Fallback_snd_pcm_get_chmap(pcm);
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
    return g_real_snd_pcm_chmap_print ? g_real_snd_pcm_chmap_print(map, maxlen, buf)
                                      : Fallback_snd_pcm_chmap_print(map, maxlen, buf);
  }
} // extern "C"
