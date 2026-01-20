/*
 *  Copyright (C) 2017-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <alsa/asoundlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Channel map functions missing in old ALSA versions (pre-1.0.27)
  int snd_pcm_set_chmap(snd_pcm_t* pcm, const snd_pcm_chmap_t* map);
  snd_pcm_chmap_query_t** snd_pcm_query_chmaps(snd_pcm_t* pcm);
  snd_pcm_chmap_t* snd_pcm_get_chmap(snd_pcm_t* pcm);
  void snd_pcm_free_chmaps(snd_pcm_chmap_query_t** maps);
  int snd_pcm_chmap_print(const snd_pcm_chmap_t* map, size_t maxlen, char* buf);

#ifdef __cplusplus
}
#endif
