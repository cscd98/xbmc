/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MFSession.h"
#include "DVDStreamInfo.h"

#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Media.Protection.h>
#include <winrt/Windows.Storage.Streams.h>

#include "utils/StringUtils.h"
#include "utils/log.h"
#include "windowing/win10/MFCompositionHost.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::MediaProperties;
using namespace winrt::Windows::Media::Playback;
using namespace winrt::Windows::Storage::Streams;
using namespace std::chrono_literals;

namespace
{
//constexpr guid kSeqHeader{0x3C036DE7, 0x3AD0, 0x4c9e, {0x92, 0x16, 0xEE, 0x6D, 0x6A, 0xC2, 0x1C, 0xB3}};
constexpr size_t kMaxQueue = 12;
} // namespace

std::shared_ptr<CMFSession> CMFSession::Acquire()
{
  static std::weak_ptr<CMFSession> s_weak;
  static std::mutex s_mtx;
  std::lock_guard l(s_mtx);
  auto s = s_weak.lock();
  if (!s) { s = std::make_shared<CMFSession>(); s_weak = s; }
  return s;
}

CMFSession::~CMFSession() { Close(); }

bool CMFSession::Open(unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
                        const uint8_t* seqHdr, size_t seqHdrSize,
                        const std::vector<winrt::hstring>& candidates)
{
  for (const auto& sub : candidates)
  {
    if (OpenWith(sub, w, h, fpsRate, fpsScale, seqHdr, seqHdrSize))
    {
      CLog::LogF(LOGINFO, "MF accepted subtype {}", winrt::to_string(sub));
      return true;
    }
    Close();
  }
  return false;
}

#include <array>

bool CMFSession::OpenWith(const winrt::hstring& subtype, unsigned w, unsigned h, 
  unsigned fpsRate, unsigned fpsScale,
  const uint8_t* seqHdr, size_t seqHdrSize)
{
    CLog::LogF(LOGDEBUG,
           "MFDV: OpenWith subtype='{}' {}x{} fps={}/{}",
           winrt::to_string(subtype), w, h, fpsRate, fpsScale);

  try
  {
    const bool isDv = subtype == L"dvhe";
    if (isDv)
    {
      if (!CMFCompositionHost::Get().SetDolbyVisionOutput(true))
      {
        CLog::LogF(LOGWARNING, "MFDV: Failed to enable Dolby Vision output");
      }
    }

    // See: https://learn.microsoft.com/en-us/uwp/api/windows.media.mediaproperties.videoencodingproperties.subtype
    VideoEncodingProperties props;
    props.Subtype(L"HEVC"); // TODO: remove hard coded field, construct from ffmpeg to expected
    //props.Subtype(subtype);
    props.Width(w);
    props.Height(h);

    if (fpsRate && fpsScale)
    {
      props.FrameRate().Numerator(fpsRate);
      props.FrameRate().Denominator(fpsScale);
    }

    /*if (seqHdr && seqHdrSize)
    {
      props.Properties().Insert(
          kSeqHeader,
          PropertyValue::CreateUInt8Array(
              array_view<const uint8_t>(seqHdr, seqHdr + seqHdrSize)));
    }*/

    // DOES NOT WORK:
    /*if (isDv)
    {
      // Dolby Vision decoder configuration record:
      // version 1.0, profile 5, level 9,
      // RPU present, no EL, BL present,
      // compatibility ID 0, no compression.
      std::array<uint8_t, 24> doviConfig{};

      doviConfig[0] = 1; // dv_version_major
      doviConfig[1] = 0; // dv_version_minor

      const uint16_t profileLevel =
          static_cast<uint16_t>((5 << 9) | (9 << 3) | (1 << 2) | (0 << 1) | 1);

      doviConfig[2] = static_cast<uint8_t>(profileLevel >> 8);
      doviConfig[3] = static_cast<uint8_t>(profileLevel & 0xff);

      // compatibility_id = 0, compression = 0
      doviConfig[4] = 0;

      props.SetFormatUserData(
          winrt::array_view<const uint8_t>(doviConfig.data(),
                                            doviConfig.data() + doviConfig.size()));

      CLog::LogF(
          LOGINFO,
          "MFDV: Set Dolby Vision config: "
          "version=1.0 profile=5 level=9 RPU=1 EL=0 BL=1 compat=0 compression=0");*/

    //}

    CLog::LogF(LOGDEBUG, "MFDV: Props completed");

    m_desc = VideoStreamDescriptor(props);

    m_failed = m_opened = false;
    m_player = MediaPlayer();
    m_player.CommandManager().IsEnabled(false);
    m_player.RealTimePlayback(true);
    m_player.AutoPlay(false);

    m_player.MediaFailed([this](auto&&, MediaPlayerFailedEventArgs const& a)
    {
      CLog::LogF(LOGERROR, "MediaFailed: error {}, hr {:#x}, '{}'", static_cast<int>(a.Error()),
                 static_cast<uint32_t>(static_cast<int32_t>(a.ExtendedErrorCode())),
                 winrt::to_string(a.ErrorMessage()));
      { std::lock_guard l(m_evtMtx); m_failed = true; }
      m_evtCv.notify_all();
    });
    m_player.MediaOpened([this](auto&&, auto&&)
    {
      CLog::LogF(LOGINFO, "MediaOpened!");
      { std::lock_guard l(m_evtMtx); m_opened = true; }
      m_evtCv.notify_all();
    });
    auto session = m_player.PlaybackSession();
    session.PlaybackStateChanged([](MediaPlaybackSession const& s, auto&&)
    { CLog::LogF(LOGINFO, "playback state {}", static_cast<int>(s.PlaybackState())); });
    session.BufferingStarted([](auto&&, auto&&) { CLog::LogF(LOGINFO, "buffering started"); });
    session.BufferingEnded([](auto&&, auto&&) { CLog::LogF(LOGINFO, "buffering ended"); });

    CreateSource();

    // Probe: an explicit failure (no decoder / rejected type) means -> DXVA fallback.
    // A timeout is treated as success: MediaOpened may need prerolled samples.
    std::unique_lock l(m_evtMtx);
    m_evtCv.wait_for(l, 1500ms, [this] { return m_failed || m_opened; });
    if (m_failed)
    {
      l.unlock();
      Close();
      return false;
    }
    return true;
  }
  catch (const hresult_error& e)
  {
    CLog::LogF(LOGERROR, "MF open failed: {:#x}", static_cast<uint32_t>(e.code()));
    Close();
    return false;
  }
}

bool CMFSession::ConfigureAudio(const CDVDStreamInfo& hints)
{
  if (!m_player || !m_mss || hints.codec != AV_CODEC_ID_AAC || hints.samplerate <= 0 ||
      hints.channels <= 0 || !hints.extradata)
    return false;

  try
  {
    auto properties = AudioEncodingProperties::CreateAac(
        hints.samplerate, hints.channels, hints.bitrate > 0 ? hints.bitrate : 128000);
    properties.SetFormatUserData(array_view<const uint8_t>(
        hints.extradata.GetData(), hints.extradata.GetData() + hints.extradata.GetSize()));
    m_audioDesc = AudioStreamDescriptor(properties);

    // A new source is required because stream descriptors cannot be added while the
    // existing source is attached to the MediaPlayer pipeline.
    Restart();
    return true;
  }
  catch (const hresult_error& e)
  {
    CLog::LogF(LOGWARNING, "MFDV: AAC stream setup failed: {:#x}",
               static_cast<uint32_t>(e.code()));
    m_audioDesc = nullptr;
    try
    {
      Restart();
    }
    catch (const hresult_error& restoreError)
    {
      CLog::LogF(LOGERROR, "MFDV: Failed to restore video-only source: {:#x}",
                 static_cast<uint32_t>(restoreError.code()));
      Close();
    }
    return false;
  }
}

void CMFSession::CreateSource()
{
  {
    std::lock_guard l(m_ctlMtx);
    m_baseUs = -1.0; m_started = false; m_lastPts = -1.0; m_slew = 1.0;
    m_pushed = m_delivered = m_rendered = 0;
  }

  CLog::LogF(LOGINFO, "MFDV: Creating MediaStreamSource");
  m_mss = m_audioDesc ? MediaStreamSource(m_desc, m_audioDesc) : MediaStreamSource(m_desc);
  CLog::LogF(LOGINFO, "MFDV: MediaStreamSource created");
  m_mss.BufferTime(0s);
  m_mss.CanSeek(false);
  m_mss.Starting([](auto&&, MediaStreamSourceStartingEventArgs const& a)
                 { a.Request().SetActualStartPosition(TimeSpan{0}); });
  m_mss.SampleRequested([this](auto&&, MediaStreamSourceSampleRequestedEventArgs const& a)
  {
    std::lock_guard l(m_mtx);
    auto req = a.Request();
    const size_t streamIndex = req.StreamDescriptor().try_as<AudioStreamDescriptor>() ? 1 : 0;
    if (!m_queues[streamIndex].empty())
    {
      req.Sample(m_queues[streamIndex].front());
      m_queues[streamIndex].pop_front();
      const uint64_t n = ++m_delivered;
      if (n == 1 || n % 120 == 0)
        CLog::LogF(LOGINFO, "MF consumed {} samples (pushed {})", n, m_pushed.load());
    }
    else
    {
      m_pendingRequests[streamIndex].push_back({req, req.GetDeferral()});
    }
  });
  m_mss.SampleRendered([this](auto&&, MediaStreamSourceSampleRenderedEventArgs const& a)
  {
    const uint64_t n = ++m_rendered;
    if (n == 1 || n % 120 == 0)
      CLog::LogF(LOGDEBUG, "MF rendered sample #{} lag {} ms", n,
                 std::chrono::duration_cast<std::chrono::milliseconds>(a.SampleLag()).count());
  });
  m_mss.Closed([](auto&&, MediaStreamSourceClosedEventArgs const& a)
  { CLog::LogF(LOGWARNING, "MSS closed, reason {}", static_cast<int>(a.Request().Reason())); });

  CLog::LogF(LOGINFO, "MFDV: Creating MediaSource");
  m_player.Source(MediaSource::CreateFromMediaStreamSource(m_mss));
  CLog::LogF(LOGINFO, "MFDV: MediaSource created");
}

void CMFSession::Close()
{
  CMFCompositionHost::Get().HideVideo();
  if (m_player)
  {
    m_player.Source(nullptr);
    m_player.Close();
    m_player = nullptr;
  }
  std::lock_guard l(m_mtx);
  for (size_t i = 0; i < m_queues.size(); ++i)
  {
    m_queues[i].clear();
    m_pendingRequests[i].clear();
  }
  m_audioDesc = nullptr;
  m_desc = nullptr;
}

void CMFSession::Restart()
{
  if (!m_player) return;
  {
    std::lock_guard l(m_ctlMtx);
    if (m_player.PlaybackSession().PlaybackState() ==
        winrt::Windows::Media::Playback::MediaPlaybackState::Playing)
      m_player.Pause();
  }
  m_player.Source(nullptr);
  m_mss = nullptr;
  if (m_desc)
    m_desc = VideoStreamDescriptor(m_desc.EncodingProperties().Copy());
  if (m_audioDesc)
    m_audioDesc = m_audioDesc.Copy();
  {
    std::lock_guard l(m_mtx);
    for (size_t i = 0; i < m_queues.size(); ++i)
    {
      m_queues[i].clear();
      for (auto& pending : m_pendingRequests[i])
      {
        if (pending.deferral)
          pending.deferral.Complete();
      }
      m_pendingRequests[i].clear();
    }
  }
  CreateSource(); // an MSS cannot be flushed; a new source is the reliable "flush"
}

bool CMFSession::CanQueue(bool audio)
{
  std::lock_guard l(m_mtx);
  return m_queues[audio ? 1 : 0].size() < kMaxQueue;
}

void CMFSession::Push(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key)
{
  PushSample(d, n, dtsUs, ptsUs, durUs, key, 0);
}

void CMFSession::PushAudio(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs)
{
  PushSample(d, n, dtsUs, ptsUs, durUs, false, 1);
}

void CMFSession::PushSample(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs,
                            bool key, size_t streamIndex)
{
  if (m_baseUs.load() < 0)
  {
    m_baseUs = dtsUs;
    std::string hex;
    for (size_t i = 0; i < std::min<size_t>(n, 24); ++i) hex += StringUtils::Format("{:02x} ", d[i]);
    CLog::LogF(LOGDEBUG, "first sample: {} bytes, key {}, base dts {:.0f} us, head: {}", n, key, dtsUs, hex);
  }
  const double base = m_baseUs;

  DataWriter w;
  w.WriteBytes(array_view<const uint8_t>(d, d + n));
  auto sample = MediaStreamSample::CreateFromBuffer(
      w.DetachBuffer(), TimeSpan{static_cast<int64_t>(std::max(0.0, ptsUs - base) * 10)});
  sample.DecodeTimestamp(TimeSpan{static_cast<int64_t>(std::max(0.0, dtsUs - base) * 10)});
  sample.Duration(TimeSpan{static_cast<int64_t>(durUs * 10)});
  sample.KeyFrame(key);
  ++m_pushed;

  std::lock_guard l(m_mtx);
  auto& pendingRequests = m_pendingRequests[streamIndex];
  if (!pendingRequests.empty())
  {
    auto pending = std::move(pendingRequests.front());
    pendingRequests.pop_front();
    pending.request.Sample(sample);
    pending.deferral.Complete();
    ++m_delivered;
  }
  else
    m_queues[streamIndex].push_back(sample);
}

void CMFSession::AttachSurface(float srcW, float srcH)
{
  auto& host = CMFCompositionHost::Get();
  m_player.SetSurfaceSize({srcW, srcH});
  host.ShowVideo(m_player.GetSurface(host.GetCompositor()));  
  CLog::LogF(LOGINFO, "surface attached, source {}x{}", srcW, srcH);
}

void CMFSession::SetDestRect(float x, float y, float w, float h)
{
  CMFCompositionHost::Get().SetVideoRect(x, y, w, h);
}

void CMFSession::ApplyRate() // ctl lock held
{
  if (m_speed <= 0 || m_speed > 2 * DVD_PLAYSPEED_NORMAL)
    return;
  try
  {
    m_player.PlaybackSession().PlaybackRate(m_slew * m_speed / static_cast<double>(DVD_PLAYSPEED_NORMAL));
  }
  catch (const winrt::hresult_error& e)
  {
    CLog::LogF(LOGWARNING, "PlaybackRate rejected: {:#x}", static_cast<uint32_t>(e.code()));
  }
}

void CMFSession::SetSpeed(int speed)
{
  std::lock_guard l(m_ctlMtx);
  m_speed = speed;
  if (!m_player) return;
  using St = winrt::Windows::Media::Playback::MediaPlaybackState;
  const auto state = m_player.PlaybackSession().PlaybackState();
  if (speed <= 0 || speed > 2 * DVD_PLAYSPEED_NORMAL)
  {
    if (state == St::Playing) m_player.Pause();
  }
  else
  {
    ApplyRate();
    if (m_started && state != St::Playing) m_player.Play();
  }
}

void CMFSession::OnFrame(double ptsUs)
{
  std::lock_guard l(m_ctlMtx);
  const double base = m_baseUs;
  if (!m_player || base < 0 || ptsUs == m_lastPts) return;
  m_lastPts = ptsUs;

  if (!m_started)
  {
    ApplyRate();
    if (m_speed > 0) m_player.Play();
    m_started = true;
    m_lastLog = std::chrono::steady_clock::now();
    CLog::LogF(LOGDEBUG, "timeline started at kodi pts {:.0f} us (base {:.0f}), speed {}", ptsUs, base, m_speed);
    return;
  }
  if (m_speed != DVD_PLAYSPEED_NORMAL) return;

  const int64_t want = static_cast<int64_t>((ptsUs - base) * 10);
  const double driftMs =
      (m_player.PlaybackSession().Position().count() - want) / 10000.0;
  m_slew = driftMs > 40 ? 0.97 : driftMs < -40 ? 1.03 : (std::abs(driftMs) < 10 ? 1.0 : m_slew);
  ApplyRate();
  const double requestedRate = m_slew * m_speed / static_cast<double>(DVD_PLAYSPEED_NORMAL);
  const double effectiveRate = m_player.PlaybackSession().PlaybackRate();

  const auto now = std::chrono::steady_clock::now();
  if (now - m_lastLog > 2s)
  {
    m_lastLog = now;
    CLog::LogF(LOGDEBUG,
               "drift {:.1f} ms, rate requested {:.4f}, effective {:.4f}, pushed {}, consumed "
               "{}, rendered {}",
               driftMs, requestedRate, effectiveRate, m_pushed.load(), m_delivered.load(),
               m_rendered.load());
  }
}
