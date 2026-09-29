// DVDVideoCodecMFDV.cpp
#include "DVDVideoCodecMFDV.h"

#include "DVDCodecs/DVDFactoryCodec.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/SystemInfo.h"
#include "utils/log.h"

bool CDVDVideoCodecMFDV::Register()
{
  CDVDFactoryCodec::RegisterHWVideoCodec(
      "mfdv", [](CProcessInfo& p) -> std::unique_ptr<CDVDVideoCodec>
      { return std::make_unique<CDVDVideoCodecMFDV>(p); });
  return true;
}

CDVDVideoCodecMFDV::~CDVDVideoCodecMFDV()
{
  if (m_session)
    m_session->Close();
}

bool CDVDVideoCodecMFDV::Open(CDVDStreamInfo& hints, CDVDCodecOptions&)
{
  if (CSysInfo::GetWindowsDeviceFamily() != CSysInfo::Xbox ||
      !CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool("videoplayer.usemfdolbyvision"))
    return false;

  // Only what DXVA cannot do: HEVC Dolby Vision profile 5 with an hvcC.
  // hints.dovi: see note 3 below (verify it is copied into CDVDStreamInfo)
  if (hints.codec != AV_CODEC_ID_HEVC || hints.hdrType != StreamHdrType::HDR_TYPE_DOLBYVISION ||
      hints.dovi.dv_profile != 5 || !hints.extradata || hints.extradata.GetSize() < 23)
    return false;

  if (!CMFDVSession::IsDecoderAvailable())
  {
    CLog::LogF(LOGWARNING, "Dolby Vision decoder extension is not installed");
    return false;
  }

  m_hints = hints;
  m_nalLen = (hints.extradata.GetData()[21] & 3) + 1; // hvcC lengthSizeMinusOne

  m_session = CMFDVSession::Acquire();
  if (!m_session->Open(hints.width, hints.height, hints.fpsrate, hints.fpsscale,
                       hints.extradata.GetData(), hints.extradata.GetSize()))
    return false;

  m_processInfo.SetVideoDecoderName("mfdv (Dolby Vision)", true);
  m_processInfo.SetVideoPixelFormat("dolbyvision");
  m_processInfo.SetVideoDimensions(hints.width, hints.height);
  return true;
}

bool CDVDVideoCodecMFDV::IsKeyframe(const uint8_t* d, int n) const
{
  for (int p = 0; p + m_nalLen + 2 <= n;)
  {
    uint32_t len = 0;
    for (int i = 0; i < m_nalLen; i++) len = (len << 8) | d[p + i];
    const int type = (d[p + m_nalLen] >> 1) & 0x3f;
    if (type >= 16 && type <= 23) return true; // IRAP
    p += m_nalLen + len;
  }
  return false;
}

bool CDVDVideoCodecMFDV::AddData(const DemuxPacket& pkt)
{
  if (!pkt.pData || pkt.iSize <= 0)
    return true;
  if (!m_session->CanQueue())
    return false;

  double dur = pkt.duration;
  if (dur <= 0 && m_hints.fpsrate > 0)
    dur = DVD_TIME_BASE * static_cast<double>(m_hints.fpsscale) / m_hints.fpsrate;

  double dts = pkt.dts, pts = pkt.pts;
  if (dts == DVD_NOPTS_VALUE) dts = pts;
  if (pts == DVD_NOPTS_VALUE) pts = dts;
  if (dts == DVD_NOPTS_VALUE) // no timestamps at all: extrapolate
    dts = pts = (m_lastPts == DVD_NOPTS_VALUE ? 0.0 : m_lastPts + dur);
  m_lastPts = pts;

  m_session->Push(pkt.pData, pkt.iSize, dts, pts, dur, IsKeyframe(pkt.pData, pkt.iSize));
  m_reorder.emplace(pts, Pending{pts, dts, dur});
  return true;
}

void CDVDVideoCodecMFDV::Reset()
{
  m_reorder.clear();
  m_session->Restart();
}

CDVDVideoCodec::VCReturn CDVDVideoCodecMFDV::GetPicture(VideoPicture* p)
{
  const bool drain = (m_ctrl & DVD_CODEC_CTRL_DRAIN) != 0;
  if (m_reorder.empty())
    return drain ? VC_EOF : VC_BUFFER;
  if (m_reorder.size() <= m_reorderDepth && !drain)
    return VC_BUFFER;

  const Pending pd = m_reorder.begin()->second;
  m_reorder.erase(m_reorder.begin());

  p->Reset();
  p->pts = pd.pts;
  p->dts = pd.dts;
  p->iDuration = pd.dur;
  p->iWidth = m_hints.width;
  p->iHeight = m_hints.height;
  p->iDisplayHeight = p->iHeight;
  p->iDisplayWidth = (static_cast<int>(p->iHeight * m_hints.aspect + 0.5)) & -3;
  if (p->iDisplayWidth > p->iWidth)
    p->iDisplayWidth = p->iWidth;
  p->color_primaries = p->m_originalColorPrimaries = m_hints.colorPrimaries;
  p->color_transfer = m_hints.colorTransferCharacteristic;
  p->color_space = m_hints.colorSpace;
  p->color_range = m_hints.colorRange == AVCOL_RANGE_JPEG ? 1 : 0;
  p->colorBits = 10;
  p->pixelFormat = AV_PIX_FMT_YUV420P10;
  p->hdrType = m_hints.hdrType;
  if (m_hints.masteringMetadata) { p->displayMetadata = *m_hints.masteringMetadata; p->hasDisplayMetadata = true; }
  if (m_hints.contentLightMetadata) { p->lightMetadata = *m_hints.contentLightMetadata; p->hasLightMetadata = true; }
  p->videoBuffer = m_pool->Get();
  return VC_PICTURE;
}
