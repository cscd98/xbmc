/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include "cores/VideoPlayer/Interface/TimingConstants.h"

class CDVDStreamInfo;

class CMFSession
{
public:
  static std::shared_ptr<CMFSession> Acquire();
  ~CMFSession();

  bool Open(unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
            const uint8_t* seqHdr, size_t seqHdrSize, const std::vector<winrt::hstring>& candidate);
  bool OpenWith(const winrt::hstring& subtype, unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
                const uint8_t* seqHdr, size_t seqHdrSize);
  bool ConfigureAudio(const CDVDStreamInfo& hints);
  void Close();
  void Restart();
  bool CanQueue(bool audio = false);
  void Push(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key);
  void PushAudio(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs);
  void AttachSurface(float srcW, float srcH);
  void SetDestRect(float x, float y, float w, float h);
  void OnFrame(double ptsUs);
  void SetSpeed(int speed);

private:
  struct PendingRequest
  {
    winrt::Windows::Media::Core::MediaStreamSourceSampleRequest request{nullptr};
    winrt::Windows::Media::Core::MediaStreamSourceSampleRequestDeferral deferral{nullptr};
  };

  void CreateSource();
  void ApplyRate();
  void PushSample(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key,
                  size_t streamIndex);

  winrt::Windows::Media::Playback::MediaPlayer m_player{nullptr};
  winrt::Windows::Media::Core::MediaStreamSource m_mss{nullptr};
  winrt::Windows::Media::Core::VideoStreamDescriptor m_desc{nullptr};
  winrt::Windows::Media::Core::AudioStreamDescriptor m_audioDesc{nullptr};

  std::mutex m_mtx;
  std::array<std::deque<winrt::Windows::Media::Core::MediaStreamSample>, 2> m_queues;
  std::array<std::deque<PendingRequest>, 2> m_pendingRequests;

  std::mutex m_ctlMtx;
  int m_speed{DVD_PLAYSPEED_NORMAL};
  double m_slew{1.0};
  double m_lastPts{-1.0};
  bool m_started{false};
  std::chrono::steady_clock::time_point m_lastLog;

  std::atomic<double> m_baseUs{-1.0};
  std::atomic<uint64_t> m_pushed{0}, m_delivered{0}, m_rendered{0};

  std::mutex m_evtMtx;
  std::condition_variable m_evtCv;
  bool m_failed{false};
  bool m_opened{false};
};
