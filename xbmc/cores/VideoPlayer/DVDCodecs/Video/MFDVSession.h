// xbmc/cores/VideoPlayer/DVDCodecs/Video/MFDVSession.h
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>

class CMFDVSession
{
public:
  static std::shared_ptr<CMFDVSession> Acquire(); // codec and renderer share one instance
  static bool IsDecoderAvailable();
  ~CMFDVSession();

  bool Open(unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
            const uint8_t* hvcc, size_t hvccSize);
  void Close();
  void Restart();                       // on Kodi flush/seek
  bool CanQueue() const;
  void Push(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key);

  // renderer side
  void AttachSurface(float srcW, float srcH);
  void SetDestRect(float x, float y, float w, float h);
  void OnFrame(double ptsUs)
  //void OnFrame(int index, double ptsUs);
  void ApplyRate();
  void SetSpeed(int speed);

private:
  void CreateSource();

  winrt::Windows::Media::Playback::MediaPlayer m_player{nullptr};
  winrt::Windows::Media::Playback::MediaTimelineController m_timeline{nullptr};
  winrt::Windows::Media::Core::MediaStreamSource m_mss{nullptr};
  winrt::Windows::Media::Core::VideoStreamDescriptor m_desc{nullptr};

  std::mutex m_mtx;
  std::deque<winrt::Windows::Media::Core::MediaStreamSample> m_queue;
  winrt::Windows::Media::Core::MediaStreamSourceSampleRequest m_pendingReq{nullptr};
  winrt::Windows::Media::Core::MediaStreamSourceSampleRequestDeferral m_deferral{nullptr};

  std::mutex m_ctlMtx;             // guards m_baseUs, m_started, m_lastPts, m_speed, m_slew, timeline calls
    int m_speed{DVD_PLAYSPEED_NORMAL};
    double m_slew{1.0};
    double m_lastPts{-1.0};

  double m_baseUs{-1.0};
  bool m_started{false};
  int m_lastIndex{-1};
  std::chrono::steady_clock::time_point m_lastChange;
  double m_latencyUs{60000.0}; // MF present latency; tune on device
};
