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
#include "MFPacketSource.h"

#include <dxgi1_5.h>
#include <wrl/client.h>

class CMFSession
{
public:
  static std::shared_ptr<CMFSession> Acquire();
  ~CMFSession();

  bool Open(unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
      const uint8_t* seqHdr, size_t seqHdrSize,
      const std::vector<winrt::hstring>& candidate,
      const winrt::hstring& rendererExtensionProfile);
  bool OpenWith(const winrt::hstring& subtype, unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
          const uint8_t* seqHdr, size_t seqHdrSize,
          const winrt::hstring& rendererExtensionProfile);
  void Close();
  void Restart();
  bool CanQueue();
  HRESULT SetSequenceHeader(const uint8_t* data, size_t size);
  void Push(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key);
  void AttachSurface(float srcW, float srcH);
  void SetDestRect(float x, float y, float w, float h);
  void OnFrame(double ptsUs);
  void SetSpeed(int speed);

private:
  void CreateSource();
  HRESULT StartNativeEngineSource();
  bool CreateNativeEngine(unsigned width, unsigned height, unsigned fpsRate, unsigned fpsScale,
                          const winrt::hstring& rendererExtensionProfile,
                          const uint8_t* sequenceHeader, size_t sequenceHeaderSize);
  void ApplyRate();

  Microsoft::WRL::ComPtr<IMFMediaEngine> m_engine;
  Microsoft::WRL::ComPtr<IMFMediaEngineEx> m_engineEx;
  Microsoft::WRL::ComPtr<IMFMediaEngineNotify> m_engineNotify;
  Microsoft::WRL::ComPtr<CMFPacketSource> m_packetSource;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> m_videoSwapChain;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_videoSwapChain3;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_dxgiManager;
  unsigned m_nativeWidth{0};
  unsigned m_nativeHeight{0};
  unsigned m_nativeFpsRate{0};
  unsigned m_nativeFpsScale{0};
  std::vector<uint8_t> m_nativeSequenceHeader;
  winrt::hstring m_nativeRendererProfile;
  bool m_useNativeEngine{false};
  bool m_nativeSourceStarted{false};
  bool m_mfStarted{false};

  winrt::Windows::Media::Playback::MediaPlayer m_player{nullptr};
  winrt::Windows::Media::Core::MediaStreamSource m_mss{nullptr};
  winrt::Windows::Media::Core::VideoStreamDescriptor m_desc{nullptr};
  std::mutex m_mtx;
  std::deque<winrt::Windows::Media::Core::MediaStreamSample> m_queue;
  winrt::Windows::Media::Core::MediaStreamSourceSampleRequest m_pendingReq{nullptr};
  winrt::Windows::Media::Core::MediaStreamSourceSampleRequestDeferral m_deferral{nullptr};

  std::mutex m_ctlMtx;
  int m_speed{DVD_PLAYSPEED_NORMAL};
  double m_slew{1.0};
  double m_lastPts{-1.0};
  bool m_started{false};
  double m_latencyUs{60000.0};
  std::chrono::steady_clock::time_point m_lastLog;

  std::atomic<double> m_baseUs{-1.0};
  std::atomic<uint64_t> m_pushed{0}, m_delivered{0}, m_rendered{0};

  std::mutex m_evtMtx;
  std::condition_variable m_evtCv;
  bool m_failed{false};
  bool m_opened{false};
};
