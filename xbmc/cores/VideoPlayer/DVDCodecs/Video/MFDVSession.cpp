// MFDVSession.cpp
#include "MFDVSession.h"

#include "platform/win10/AsyncHelpers.h"   // Wait()
#include "windowing/win10/MFDVCompositionHost.h"

#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::Playback;
using namespace winrt::Windows::Storage::Streams;
using namespace std::chrono_literals;

namespace
{
// MFVideoFormat_DVHE ('dvhe') and MFVideoFormat_DVH1 ('dvh1')
constexpr wchar_t kDvhe[] = L"{65687664-0000-0010-8000-00AA00389B71}";
constexpr wchar_t kDvh1[] = L"{31687664-0000-0010-8000-00AA00389B71}";
// MF_MT_MPEG_SEQUENCE_HEADER
constexpr guid kSeqHeader{0x3C036DE7, 0x3AD0, 0x4c9e, {0x92, 0x16, 0xEE, 0x6D, 0x6A, 0xC2, 0x1C, 0xB3}};
constexpr size_t kMaxQueue = 12;
} // namespace

std::shared_ptr<CMFDVSession> CMFDVSession::Acquire()
{
  static std::weak_ptr<CMFDVSession> s_weak;
  static std::mutex s_mtx;
  std::lock_guard l(s_mtx);
  auto s = s_weak.lock();
  if (!s)
  {
    s = std::make_shared<CMFDVSession>();
    s_weak = s;
  }
  return s;
}

bool CMFDVSession::IsDecoderAvailable()
{
  try
  {
    CodecQuery q;
    for (auto sub : {kDvhe, kDvh1})
      if (Wait(q.FindAllAsync(CodecKind::Video, CodecCategory::Decoder, sub)).Size() > 0)
        return true;
  }
  catch (const hresult_error&) {}
  return false;
}

CMFDVSession::~CMFDVSession() { Close(); }

bool CMFDVSession::Open(unsigned w, unsigned h, unsigned fpsRate, unsigned fpsScale,
                        const uint8_t* hvcc, size_t hvccSize)
{
  try
  {
    // "dvhe" = parameter sets in-band, "dvh1" = out-of-band; try dvhe first
    VideoEncodingProperties props;
    props.Subtype(kDvhe);
    props.Width(w);
    props.Height(h);
    if (fpsRate && fpsScale)
    {
      props.FrameRate().Numerator(fpsRate);
      props.FrameRate().Denominator(fpsScale);
    }
    props.Properties().Insert(kSeqHeader,
        PropertyValue::CreateUInt8Array(array_view<const uint8_t>(hvcc, hvcc + hvccSize)));
    m_desc = VideoStreamDescriptor(props);

    m_player = MediaPlayer();
    m_player.CommandManager().IsEnabled(false);
    m_player.RealTimePlayback(true);
    m_player.AutoPlay(false);
    m_timeline = MediaTimelineController();
    m_player.TimelineController(m_timeline);

    CreateSource();
    return true;
  }
  catch (const hresult_error& e)
  {
    CLog::LogF(LOGERROR, "MF DV open failed: {:#x}", static_cast<uint32_t>(e.code()));
    Close();
    return false;
  }
}

void CMFDVSession::CreateSource()
{
  m_mss = MediaStreamSource(m_desc);
  m_mss.BufferTime(0s);
  m_mss.CanSeek(false);

  m_mss.Starting([](auto&&, MediaStreamSourceStartingEventArgs const& a)
                 { a.Request().SetActualStartPosition(TimeSpan{0}); });

  m_mss.SampleRequested(
      [this](auto&&, MediaStreamSourceSampleRequestedEventArgs const& a)
      {
        std::lock_guard l(m_mtx);
        auto req = a.Request();
        if (!m_queue.empty())
        {
          req.Sample(m_queue.front());
          m_queue.pop_front();
        }
        else
        {
          m_pendingReq = req;
          m_deferral = req.GetDeferral();
        }
      });

  m_baseUs = -1.0;
  m_started = false;
  m_lastIndex = -1;
  m_player.Source(MediaSource::CreateFromMediaStreamSource(m_mss));
}

void CMFDVSession::Close()
{
  CMFDVCompositionHost::Get().HideVideo();
  if (m_player)
  {
    m_player.Source(nullptr);
    m_player.Close();
    m_player = nullptr;
  }
  std::lock_guard l(m_mtx);
  m_queue.clear();
  m_pendingReq = nullptr;
  m_deferral = nullptr;
}

void CMFDVSession::Restart()
{
  if (!m_player)
    return;
  if (m_timeline.State() == MediaTimelineControllerState::Running)
    m_timeline.Pause();
  {
    std::lock_guard l(m_mtx);
    m_queue.clear();
    if (m_deferral)
    {
      m_deferral.Complete(); // release the outstanding request without a sample
      m_deferral = nullptr;
      m_pendingReq = nullptr;
    }
  }
  CreateSource(); // MSS cannot be flushed; a new source is the reliable "flush"
}

bool CMFDVSession::CanQueue() const { return m_queue.size() < kMaxQueue; }

void CMFDVSession::Push(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key)
{
  if (m_baseUs < 0)
    m_baseUs = dtsUs;

  DataWriter w;
  w.WriteBytes(array_view<const uint8_t>(d, d + n));
  auto sample = MediaStreamSample::CreateFromBuffer(w.DetachBuffer(),
                    TimeSpan{static_cast<int64_t>((ptsUs - m_baseUs) * 10)}); // µs -> 100 ns
  sample.DecodeTimestamp(TimeSpan{static_cast<int64_t>((dtsUs - m_baseUs) * 10)});
  sample.Duration(TimeSpan{static_cast<int64_t>(durUs * 10)});
  sample.KeyFrame(key);

  std::lock_guard l(m_mtx);
  if (m_deferral)
  {
    m_pendingReq.Sample(sample);
    m_deferral.Complete();
    m_deferral = nullptr;
    m_pendingReq = nullptr;
  }
  else
    m_queue.push_back(sample);
}

void CMFDVSession::AttachSurface(float srcW, float srcH)
{
  auto& host = CMFDVCompositionHost::Get();
  m_player.SetSurfaceSize({srcW, srcH});
  host.ShowVideo(m_player.GetSurface(host.GetCompositor()));
}

void CMFDVSession::SetDestRect(float x, float y, float w, float h)
{
  CMFDVCompositionHost::Get().SetVideoRect(x, y, w, h);
}

void CMFDVSession::ApplyRate()   // ctl lock held
{
  if (m_speed > 0 && m_speed <= 2 * DVD_PLAYSPEED_NORMAL)
    m_timeline.ClockRate(m_slew * m_speed / static_cast<double>(DVD_PLAYSPEED_NORMAL));
}

void CMFDVSession::SetSpeed(int speed)
{
  std::lock_guard l(m_ctlMtx);
  m_speed = speed;
  if (!m_player)
    return;
  if (speed <= 0 || speed > 2 * DVD_PLAYSPEED_NORMAL) // pause / rewind / fast-forward:
  {                                                   // MF cannot follow, freeze the last frame
    if (m_timeline.State() == MediaTimelineControllerState::Running)
      m_timeline.Pause();
  }
  else
  {
    ApplyRate();
    if (m_started && m_timeline.State() != MediaTimelineControllerState::Running)
      m_timeline.Resume();
  }
}

// Kodi is the clock master: MF free-runs and is nudged by slewing ClockRate (no seeks,
// because the MSS is not seekable). Pause is detected when the frame index stops changing.
void CMFDVSession::OnFrame(double ptsUs)
{
  std::lock_guard l(m_ctlMtx);
  if (!m_player || m_baseUs < 0 || ptsUs == m_lastPts)
    return;
  m_lastPts = ptsUs;

  const int64_t want = static_cast<int64_t>((ptsUs - m_baseUs + m_latencyUs) * 10);
  if (!m_started)
  {
    m_timeline.Position(TimeSpan{want});
    ApplyRate();
    if (m_speed > 0)
      m_timeline.Resume();          // NOT Start(): Start() begins at zero
    m_started = true;
    return;
  }
  if (m_speed != DVD_PLAYSPEED_NORMAL)
    return;

  const double driftMs = (m_timeline.Position().count() - want) / 10000.0; // >0: MF is ahead
  m_slew = driftMs > 40 ? 0.97 : driftMs < -40 ? 1.03 : (std::abs(driftMs) < 10 ? 1.0 : m_slew);
  ApplyRate();
}
