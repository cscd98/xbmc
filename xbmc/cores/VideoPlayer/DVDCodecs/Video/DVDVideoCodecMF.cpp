/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecMF.h"

#include "DVDCodecs/DVDFactoryCodec.h"
#include "rendering/dx/DeviceResources.h"
#include "ServiceBroker.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/SystemInfo.h"
#include "utils/log.h"

#include <algorithm>
#include <cwchar>

#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <propvarutil.h>
#include <wrl/client.h>

namespace
{
using Microsoft::WRL::ComPtr;

constexpr GUID CLSID_DV_RENDERER_CATEGORY =
{
    0x145cd8b4, 0x92f4, 0x4b23,
    {0x8a, 0xe7, 0xe0, 0xdf, 0x06, 0xc2, 0xda, 0x95}
};

constexpr GUID MFT_ENUM_VIDEO_RENDERER_EXTENSION_PROFILE =
{
    0x62c56928, 0x9a4e, 0x443b,
    {0xb9, 0xdc, 0xca, 0xc8, 0x30, 0xc2, 0x41, 0x00}
};

constexpr GUID DV_ATTR_UINT32 =
{
    0x851745d5, 0xc3d6, 0x476d,
    {0x95, 0x27, 0x49, 0x8e, 0xf2, 0xd1, 0x0d, 0x18}
};

constexpr GUID DV_ATTR_DISPLAY_NAME =
{
    0x39570660, 0x4f1c, 0x45d8,
    {0x9b, 0x0d, 0x0e, 0xf6, 0x74, 0x85, 0x3f, 0x3a}
};

// Bring-up switch: true = take every HEVC stream that has an hvcC (to test the pipeline
// with non-DV files). Set to false for release: then only Dolby Vision profile 5.
constexpr bool kBringUpAcceptAllHevc = true;

HRESULT FindDolbyVisionP5Transform(IMFTransform** result)
{
  if (!result)
    return E_POINTER;

  *result = nullptr;

  IMFActivate** activations = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFTEnumEx(CLSID_DV_RENDERER_CATEGORY, MFT_ENUM_FLAG_SORTANDFILTER, nullptr,
                         nullptr, &activations, &count);
  if (FAILED(hr))
    return hr;

  HRESULT resultHr = MF_E_TOPO_CODEC_NOT_FOUND;
  for (UINT32 i = 0; i < count; ++i)
  {
    PROPVARIANT value;
    PropVariantInit(&value);
    hr = activations[i]->GetItem(MFT_ENUM_VIDEO_RENDERER_EXTENSION_PROFILE, &value);

    bool matches = false;
    if (SUCCEEDED(hr) && value.vt == (VT_VECTOR | VT_LPWSTR))
    {
      for (ULONG j = 0; j < value.calpwstr.cElems; ++j)
      {
        const wchar_t* profile = value.calpwstr.pElems[j];
        if (profile && _wcsicmp(profile, L"dvhe.05") == 0)
        {
          matches = true;
          break;
        }
      }
    }
    PropVariantClear(&value);

    if (matches)
    {
      ComPtr<IMFTransform> transform;
      hr = activations[i]->ActivateObject(IID_PPV_ARGS(transform.GetAddressOf()));
      if (SUCCEEDED(hr))
      {
        *result = transform.Detach();
        resultHr = S_OK;
        break;
      }
      resultHr = hr;
    }
  }

  for (UINT32 i = 0; i < count; ++i)
    activations[i]->Release();
  CoTaskMemFree(activations);
  return resultHr;
}

HRESULT InitializeDVTransform(IMFTransform* transform,
                              ID3D11Device* device,
                              UINT32 attributeValue,
                              const wchar_t* displayName)
{
  if (!transform || !device)
    return E_INVALIDARG;

  UINT resetToken = 0;
  ComPtr<IMFDXGIDeviceManager> manager;
  HRESULT hr = MFCreateDXGIDeviceManager(&resetToken, manager.GetAddressOf());
  if (FAILED(hr))
    return hr;

  hr = manager->ResetDevice(device, resetToken);
  if (FAILED(hr))
    return hr;

  hr = transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                 reinterpret_cast<ULONG_PTR>(manager.Get()));
  if (FAILED(hr))
    return hr;

  ComPtr<IMFAttributes> attributes;
  hr = transform->GetAttributes(attributes.GetAddressOf());
  if (FAILED(hr))
    return hr;

  hr = attributes->SetUINT32(DV_ATTR_UINT32, attributeValue);
  if (FAILED(hr))
    return hr;

  if (displayName && *displayName)
    hr = attributes->SetString(DV_ATTR_DISPLAY_NAME, displayName);

  return hr;
}

HRESULT CheckDolbyVisionP5Transform()
{
  ComPtr<IMFTransform> transform;
  HRESULT hr = FindDolbyVisionP5Transform(transform.GetAddressOf());
  if (FAILED(hr))
    return hr;

  const auto deviceResources = DX::DeviceResources::Get();
  if (!deviceResources || !deviceResources->HasValidDevice())
    return E_FAIL;

  return InitializeDVTransform(transform.Get(), deviceResources->GetD3DDevice(), 5,
                               L"Dolby Vision Profile 5");
}

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
    const HRESULT hr = CheckDolbyVisionP5Transform();
    if (SUCCEEDED(hr))
      CLog::LogF(LOGINFO, "MFDV: Dolby Vision Profile 5 transform check succeeded");
    else
      CLog::LogF(LOGWARNING, "MFDV: Dolby Vision Profile 5 transform check failed: {:#x}",
                 static_cast<uint32_t>(hr));
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
