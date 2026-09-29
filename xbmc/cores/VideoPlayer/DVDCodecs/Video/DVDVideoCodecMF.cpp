/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecMF.h"

#include "DVDCodecs/DVDFactoryCodec.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/SystemInfo.h"
#include "utils/log.h"

#include <algorithm>

namespace
{
// Bring-up switch: true = take every HEVC stream that has an hvcC (to test the pipeline
// with non-DV files). Set to false for release: then only Dolby Vision profile 5.
constexpr bool kBringUpAcceptAllHevc = true;

bool IsIrap(const uint8_t* d, size_t n, int nalLen)
{
  for (size_t p = 0; p + nalLen + 2 <= n;)
  {
    size_t len = 0;
    for (int i = 0; i < nalLen; ++i)
      len = (len << 8) | d[p + i];
    const int type = (d[p + nalLen] >> 1) & 0x3f;
    if (type >= 16 && type <= 23)
      return true;
    p += nalLen + len;
  }
  return false;
}

}

bool CDVDVideoCodecMF::Register()
{
  CDVDFactoryCodec::RegisterHWVideoCodec(
      "mfdv", [](CProcessInfo& p) -> std::unique_ptr<CDVDVideoCodec>
      { return std::make_unique<CDVDVideoCodecMF>(p); });
  return true;
}

CDVDVideoCodecMF::~CDVDVideoCodecMF()
{
  if (m_session)
    m_session->Close();
}

bool CDVDVideoCodecMF::Open(CDVDStreamInfo& hints, CDVDCodecOptions&)
{
  CLog::LogF(LOGINFO, "MFDV: Open");
  if (CSysInfo::GetWindowsDeviceFamily() != CSysInfo::Xbox ||
      !CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool("videoplayer.usemfdolbyvision"))
    return false;

  if (!hints.extradata || hints.extradata.GetSize() < 23)
  {
    CLog::LogF(LOGWARNING, "MFDV: Missing/invalid extradata");
    return false;
  }

  // Hard guards, never bypassed: HEVC with an hvcC
  const bool isDv = hints.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION;
  const winrt::hstring subtype = isDv ? L"dvhe" : L"hevc";
  const std::vector<winrt::hstring> candidates{subtype}; // temp allow more than dv for now

  if (hints.codec != AV_CODEC_ID_HEVC)
  {
    CLog::LogF(LOGWARNING, "MFDV: Not HEVC");
    //return false;
  }

  if (isDv && hints.dovi.dv_profile == 5)
  {
    CLog::LogF(LOGWARNING, "MFDV: DV Profile 5");
    //return false;
  }

  // hvcC -> Annex-B via Kodi's converter. dvhe carries VPS/SPS/PPS in-band, so an hvcC
  // without parameter-set arrays is valid and must not fail the open.
  m_nalLen = (hints.extradata.GetData()[21] & 3) + 1; // TODO: remove?
  m_bsc.Close();
  if (!m_bsc.Open(hints.codec, hints.extradata.GetData(), hints.extradata.GetSize(), true) ||
      !m_bsc.NeedConvert())
  {
    CLog::LogF(LOGWARNING, "MFDV: BitstreamConverter open failed");
    return false;
  }
  const uint8_t* paramSets = m_bsc.GetExtraData();
  const size_t paramSize = paramSets ? static_cast<size_t>(m_bsc.GetExtraSize()) : 0;
  CLog::LogF(LOGINFO, "MFDV: hvcC parameter sets {} bytes ({})", paramSize,
             paramSize ? "out-of-band" : "in-band only");

  m_hints = hints;
  m_session = CMFSession::Acquire();
  if (!m_session->Open(hints.width, hints.height, hints.fpsrate, hints.fpsscale,
                       paramSets, paramSize, candidates))
  {
    CLog::LogF(LOGINFO, "Falling back to DXVA");
    m_session.reset();
    return false; // Kodi falls back to DXVA
  }

  CLog::LogF(LOGINFO, "Using MF decoder");

  m_processInfo.SetVideoDecoderName("mfdv", true);
  m_processInfo.SetVideoPixelFormat("Surface");
  m_processInfo.SetVideoDimensions(hints.width, hints.height);

  CLog::LogF(LOGINFO, "opened ({}), nal length size {}", isDv ? "dvhe" : "hevc", m_nalLen);
  return true;
}

bool CDVDVideoCodecMF::AddData(const DemuxPacket& pkt)
{
  if (!pkt.pData || pkt.iSize <= 0)
    return true;
  if (!m_session->CanQueue())
    return false; // backpressure: Kodi retries after GetPicture

  double dur = pkt.duration;
  if (dur <= 0 && m_hints.fpsrate > 0)
    dur = DVD_TIME_BASE * static_cast<double>(m_hints.fpsscale) / m_hints.fpsrate;

  double dts = pkt.dts, pts = pkt.pts;
  if (dts == DVD_NOPTS_VALUE) dts = pts;
  if (pts == DVD_NOPTS_VALUE) pts = dts;
  if (dts == DVD_NOPTS_VALUE) // no timestamps at all: extrapolate
    dts = pts = (m_lastPts == DVD_NOPTS_VALUE ? 0.0 : m_lastPts + dur);
  m_lastPts = pts;

  if (!m_bsc.Convert(pkt.pData, pkt.iSize) || m_bsc.GetConvertSize() <= 0)
    return true; // malformed packet: drop (GetConvertBuffer() would return the raw input)

  const bool key = IsIrap(pkt.pData, pkt.iSize, m_nalLen);
  m_session->Push(m_bsc.GetConvertBuffer(), m_bsc.GetConvertSize(), dts, pts, dur, key);
  m_reorder.emplace(pts, Pending{pts, dts, dur});
  return true;
}

void CDVDVideoCodecMF::Reset()
{
  m_lastPts = DVD_NOPTS_VALUE;
  m_reorder.clear();
  if (m_session)
    m_session->Restart();
}

CDVDVideoCodec::VCReturn CDVDVideoCodecMF::GetPicture(VideoPicture* p)
{
  const bool drain = (m_ctrl & DVD_CODEC_CTRL_DRAIN) != 0;
  if (m_reorder.empty())
    return drain ? VC_EOF : VC_BUFFER;
  if (m_reorder.size() <= m_reorderDepth && !drain)
    return VC_BUFFER;

  const Pending pd = m_reorder.begin()->second;
  m_reorder.erase(m_reorder.begin());

  p->Reset();
  p->hdrTypeAlt = StreamHdrType::HDR_TYPE_NONE;
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
  if (m_hints.masteringMetadata)
  {
    p->displayMetadata = *m_hints.masteringMetadata;
    p->hasDisplayMetadata = true;
  }
  if (m_hints.contentLightMetadata)
  {
    p->lightMetadata = *m_hints.contentLightMetadata;
    p->hasLightMetadata = true;
  }
  p->videoBuffer = m_pool->Get();
  return VC_PICTURE;
}
