/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MFSession.h"

#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>

#include <functional>
#include <cstring>
#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Media.Protection.h>
#include <winrt/Windows.Storage.Streams.h>

#include "rendering/dx/DeviceResources.h"
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
constexpr size_t kMaxQueue = 12;
constexpr GUID kDolbyVisionProfileAttribute{0x851745d5, 0xc3d6, 0x476d,
                                            {0x95, 0x27, 0x49, 0x8e, 0xf2, 0xd1, 0x0d, 0x18}};
constexpr GUID kDolbyVisionDisplayNameAttribute{0x39570660, 0x4f1c, 0x45d8,
                                                {0x9b, 0x0d, 0x0e, 0xf6, 0x74, 0x85, 0x3f, 0x3a}};

class CMFEngineNotify final
  : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                        IMFMediaEngineNotify>
{
public:
  explicit CMFEngineNotify(std::function<void(DWORD, DWORD_PTR, DWORD)> callback)
    : m_callback(std::move(callback))
  {
  }

  STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override
  {
    if (m_callback)
      m_callback(event, param1, param2);
    return S_OK;
  }

private:
  std::function<void(DWORD, DWORD_PTR, DWORD)> m_callback;
};
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
                        const std::vector<winrt::hstring>& candidates,
                        const winrt::hstring& rendererExtensionProfile)
{
  for (const auto& sub : candidates)
  {
    if (OpenWith(sub, w, h, fpsRate, fpsScale, seqHdr, seqHdrSize,
                 rendererExtensionProfile))
    {
      CLog::LogF(LOGINFO, "MF session initialized for subtype {}", winrt::to_string(sub));
      return true;
    }
    Close();
  }
  return false;
}

#include <array>

bool CMFSession::OpenWith(const winrt::hstring& subtype, unsigned w, unsigned h, 
  unsigned fpsRate, unsigned fpsScale,
  const uint8_t* seqHdr, size_t seqHdrSize,
  const winrt::hstring& rendererExtensionProfile)
{
    CLog::LogF(LOGDEBUG,
           "MFDV: OpenWith subtype='{}' {}x{} fps={}/{}",
           winrt::to_string(subtype), w, h, fpsRate, fpsScale);

  try
  {
    const bool isDv = subtype == L"dvhe";
    VideoEncodingProperties props;

    if (isDv)
    {
      CLog::LogF(LOGDEBUG,
        "MFDV composition: calling swap chain to clear hdr meta data");
      CMFCompositionHost::Get().ClearGuiSwapChainHdrMetaData();
      if (!CMFCompositionHost::Get().SetDolbyVisionOutput(true))
      {
        CLog::LogF(LOGWARNING, "MFDV: Failed to enable Dolby Vision output");
      }
      props.Subtype(L"HEVC");
    }

    if (!rendererExtensionProfile.empty())
    {
      if (!CreateNativeEngine(w, h, fpsRate, fpsScale, rendererExtensionProfile, seqHdr,
              seqHdrSize))
      {
        Close();
        return false;
      }
      return true;
    }

    // See: https://learn.microsoft.com/en-us/uwp/api/windows.media.mediaproperties.videoencodingproperties.subtype
    
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

bool CMFSession::InitializeDolbyVisionTransform()
{
  m_dvTransform.Reset();

  Microsoft::WRL::ComPtr<IMFActivate> effectActivation;
  HRESULT hr = FindDolbyVisionP5RendererEffect(effectActivation.GetAddressOf());
  if (FAILED(hr))
  {
    CLog::LogF(LOGWARNING, "MFDV: Profile 5 transform discovery failed: {:#x}",
               static_cast<uint32_t>(hr));
    return false;
  }

  hr = effectActivation->SetUINT32(kDolbyVisionProfileAttribute, 5);
  if (SUCCEEDED(hr))
    hr = effectActivation->SetString(kDolbyVisionDisplayNameAttribute,
                                     L"Dolby Vision Profile 5");
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: P5 renderer effect configuration failed: {:#x}",
               static_cast<uint32_t>(hr));
    return false;
  }

  hr = effectActivation->ActivateObject(IID_PPV_ARGS(m_dvTransform.GetAddressOf()));
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: P5 transform activation failed: {:#x}",
               static_cast<uint32_t>(hr));
    m_dvTransform.Reset();
    return false;
  }

  Microsoft::WRL::ComPtr<IMFAttributes> transformAttributes;
  hr = m_dvTransform->GetAttributes(transformAttributes.GetAddressOf());
  UINT32 d3d11Aware = FALSE;
  if (SUCCEEDED(hr))
    hr = transformAttributes->GetUINT32(MF_SA_D3D11_AWARE, &d3d11Aware);
  if (SUCCEEDED(hr) && d3d11Aware)
  {
    if (!m_dxgiManager)
    {
      CLog::LogF(LOGERROR, "MFDV: D3D11-aware P5 transform has no DXGI device manager");
      m_dvTransform.Reset();
      return false;
    }
    hr = m_dvTransform->ProcessMessage(
        MFT_MESSAGE_SET_D3D_MANAGER,
        reinterpret_cast<ULONG_PTR>(m_dxgiManager.Get()));
    if (FAILED(hr))
    {
      CLog::LogF(LOGERROR, "MFDV: P5 transform rejected DXGI device manager: {:#x}",
                 static_cast<uint32_t>(hr));
      m_dvTransform.Reset();
      return false;
    }
    CLog::LogF(LOGINFO, "MFDV: Set Kodi DXGI device manager on D3D11-aware P5 transform");
  }
  else
  {
    CLog::LogF(LOGWARNING,
               "MFDV: P5 transform did not report MF_SA_D3D11_AWARE (query hr={:#x}, value={})",
               static_cast<uint32_t>(hr), d3d11Aware);
  }

  hr = m_engineEx->InsertVideoEffect(m_dvTransform.Get(), FALSE);
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: P5 transform insertion failed: {:#x}",
               static_cast<uint32_t>(hr));
    m_dvTransform.Reset();
    return false;
  }

  CLog::LogF(LOGINFO, "MFDV: P5 IMFTransform activated and inserted into Media Engine");
  return true;
}

bool CMFSession::CreateNativeEngine(unsigned width, unsigned height, unsigned fpsRate,
                                   unsigned fpsScale,
                                   const winrt::hstring& rendererExtensionProfile,
                                   const uint8_t* sequenceHeader, size_t sequenceHeaderSize)
{
  m_baseUs = -1.0;
  m_lastPts = -1.0;
  m_slew = 1.0;
  m_started = false;
  m_pushed = m_delivered = m_rendered = 0;
  m_nativeWidth = width;
  m_nativeHeight = height;
  m_nativeFpsRate = fpsRate;
  m_nativeFpsScale = fpsScale;
  m_nativeRendererProfile = rendererExtensionProfile;
  m_nativeSequenceHeader.clear();
  if (sequenceHeader && sequenceHeaderSize)
    m_nativeSequenceHeader.assign(sequenceHeader, sequenceHeader + sequenceHeaderSize);

  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: MFStartup failed: {:#x}", static_cast<uint32_t>(hr));
    return false;
  }
  m_mfStarted = true;

  m_packetSource = Microsoft::WRL::Make<CMFPacketSource>();
  if (!m_packetSource)
    return false;
  hr = m_packetSource->Initialize(width, height, fpsRate, fpsScale,
                                  m_nativeSequenceHeader.data(), m_nativeSequenceHeader.size());
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: packet source initialization failed: {:#x}",
               static_cast<uint32_t>(hr));
    return false;
  }

  Microsoft::WRL::ComPtr<IMFMediaEngineExtension> extension;
  hr = m_packetSource->CreateEngineExtension(extension.GetAddressOf());
  if (FAILED(hr))
    return false;

  auto notify = Microsoft::WRL::Make<CMFEngineNotify>([this](DWORD event, DWORD_PTR param1,
                                                              DWORD param2)
  {
    if (event == MF_MEDIA_ENGINE_EVENT_ERROR ||
        event == MF_MEDIA_ENGINE_EVENT_STREAMRENDERINGERROR)
    {
      CLog::LogF(LOGERROR, "MFDV: Media Engine event {} failed (error {}, hr {:#x})", event,
             static_cast<uint32_t>(param1), static_cast<uint32_t>(param2));
      std::lock_guard lock(m_evtMtx);
      m_failed = true;
    }
    else if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA ||
             event == MF_MEDIA_ENGINE_EVENT_CANPLAY ||
             event == MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY)
    {
      std::lock_guard lock(m_evtMtx);
      m_opened = true;
    }
    m_evtCv.notify_all();
  });
  if (!notify)
    return false;
  m_engineNotify = notify;

  Microsoft::WRL::ComPtr<IMFAttributes> attributes;
  hr = MFCreateAttributes(attributes.GetAddressOf(), 4);
  if (FAILED(hr))
    return false;
  hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_engineNotify.Get());
  if (SUCCEEDED(hr))
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_EXTENSION, extension.Get());
  if (SUCCEEDED(hr))
    hr = attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT,
                               DXGI_FORMAT_R10G10B10A2_UNORM);
  if (FAILED(hr))
    return false;

  const auto deviceResources = DX::DeviceResources::Get();
  if (!deviceResources || !deviceResources->HasValidDevice())
    return false;
  Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
  hr = deviceResources->GetD3DDevice()->QueryInterface(IID_PPV_ARGS(multithread.GetAddressOf()));
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: D3D device does not support multithread protection: {:#x}",
               static_cast<uint32_t>(hr));
    return false;
  }
  const BOOL multithreadProtectionWasEnabled = multithread->SetMultithreadProtected(TRUE);
  CLog::LogF(LOGINFO, "MFDV: Enabled shared D3D device multithread protection (was enabled: {})",
             multithreadProtectionWasEnabled != FALSE);

  UINT resetToken = 0;
  hr = MFCreateDXGIDeviceManager(&resetToken, m_dxgiManager.GetAddressOf());
  if (SUCCEEDED(hr))
    hr = m_dxgiManager->ResetDevice(deviceResources->GetD3DDevice(), resetToken);
  if (SUCCEEDED(hr))
    hr = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, m_dxgiManager.Get());
  if (FAILED(hr))
    return false;

  Microsoft::WRL::ComPtr<IMFMediaEngineClassFactory> factory;
  hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(hr))
    return false;
  hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE, attributes.Get(),
                               m_engine.GetAddressOf());
  if (FAILED(hr))
    return false;
  hr = m_engine->QueryInterface(IID_PPV_ARGS(m_engineEx.GetAddressOf()));
  if (FAILED(hr))
    return false;

  if (!InitializeDolbyVisionTransform())
    return false;

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
  swapChainDesc.Width = width;
  swapChainDesc.Height = height;
  swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.BufferCount = 2;
  swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  hr = deviceResources->GetIDXGIFactory2()->CreateSwapChainForComposition(
      deviceResources->GetD3DDevice(), &swapChainDesc, nullptr,
      m_videoSwapChain.GetAddressOf());
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: frame-server swap chain creation failed: {:#x}",
               static_cast<uint32_t>(hr));
    return false;
  }

  hr = m_videoSwapChain->QueryInterface(IID_PPV_ARGS(m_videoSwapChain3.GetAddressOf()));
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: frame-server swap chain does not support IDXGISwapChain3");
    return false;
  }
  const auto colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
  UINT support = 0;
  hr = m_videoSwapChain3->CheckColorSpaceSupport(colorSpace, &support);
  if (FAILED(hr) || !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
  {
    CLog::LogF(LOGERROR, "MFDV: frame-server swap chain does not support HDR10 color space");
    return false;
  }
  hr = m_videoSwapChain3->SetColorSpace1(colorSpace);
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: could not set frame-server HDR color space: {:#x}",
               static_cast<uint32_t>(hr));
    return false;
  }

  m_failed = m_opened = false;
  m_nativeSourceStarted = false;
  m_useNativeEngine = true;
  CLog::LogF(LOGINFO,
             "MFDV: Native engine configured; deferring source load until sequence header and first sample are ready");
  return true;
}

HRESULT CMFSession::StartNativeEngineSource()
{
  if (m_nativeSourceStarted)
    return S_FALSE;
  if (!m_engineEx || !m_engine || m_nativeSequenceHeader.empty())
    return MF_E_NOT_INITIALIZED;

  BSTR sourceUrl = SysAllocString(L"kodi-mfdv://video");
  if (!sourceUrl)
    return E_OUTOFMEMORY;
  HRESULT hr = m_engineEx->SetSource(sourceUrl);
  SysFreeString(sourceUrl);
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: Media Engine source request failed: {:#x}",
               static_cast<uint32_t>(hr));
    return hr;
  }
  hr = m_engine->Load();
  if (FAILED(hr))
  {
    CLog::LogF(LOGERROR, "MFDV: Media Engine Load failed: {:#x}",
               static_cast<uint32_t>(hr));
    return hr;
  }
  m_nativeSourceStarted = true;
  CLog::LogF(LOGINFO, "MFDV: Media Engine source loaded after sequence header and first sample");
  return S_OK;
}

void CMFSession::CreateSource()
{
  {
    std::lock_guard l(m_ctlMtx);
    m_baseUs = -1.0; m_started = false; m_lastPts = -1.0; m_slew = 1.0;
    m_pushed = m_delivered = m_rendered = 0;
  }

  CLog::LogF(LOGINFO, "MFDV: Creating MediaStreamSource");
  m_mss = MediaStreamSource(m_desc);
  CLog::LogF(LOGINFO, "MFDV: MediaStreamSource created");
  m_mss.BufferTime(0s);
  m_mss.CanSeek(false);
  m_mss.Starting([](auto&&, MediaStreamSourceStartingEventArgs const& a)
                 { a.Request().SetActualStartPosition(TimeSpan{0}); });
  m_mss.SampleRequested([this](auto&&, MediaStreamSourceSampleRequestedEventArgs const& a)
  {
    std::lock_guard l(m_mtx);
    auto req = a.Request();
    if (!m_queue.empty())
    {
      req.Sample(m_queue.front());
      m_queue.pop_front();
      const uint64_t n = ++m_delivered;
      if (n == 1 || n % 120 == 0)
        CLog::LogF(LOGINFO, "MF consumed {} samples (pushed {})", n, m_pushed.load());
    }
    else
    {
      m_pendingReq = req;
      m_deferral = req.GetDeferral();
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
  if (m_engine)
    m_engine->Shutdown();
  m_dvTransform.Reset();
  m_engineEx.Reset();
  m_engine.Reset();
  m_engineNotify.Reset();
  m_videoSwapChain.Reset();
  m_videoSwapChain3.Reset();
  if (m_packetSource)
  {
    m_packetSource->Shutdown();
    m_packetSource.Reset();
  }
  m_dxgiManager.Reset();
  m_useNativeEngine = false;
  m_nativeSourceStarted = false;
  if (m_mfStarted)
  {
    MFShutdown();
    m_mfStarted = false;
  }
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

void CMFSession::Restart()
{
  if (m_engine)
  {
    std::lock_guard controlLock(m_ctlMtx);
    const unsigned width = m_nativeWidth;
    const unsigned height = m_nativeHeight;
    const unsigned fpsRate = m_nativeFpsRate;
    const unsigned fpsScale = m_nativeFpsScale;
    const winrt::hstring rendererProfile = m_nativeRendererProfile;
    const std::vector<uint8_t> sequenceHeader = m_nativeSequenceHeader;
    Close();
    if (CreateNativeEngine(width, height, fpsRate, fpsScale, rendererProfile,
                 sequenceHeader.data(), sequenceHeader.size()))
    {
      AttachSurface(static_cast<float>(width), static_cast<float>(height));
      return;
    }
    Close();
    CLog::LogF(LOGERROR, "MFDV: failed to restart native Media Engine");
    return;
  }
  if (!m_player)
    return;
  {
    std::lock_guard l(m_ctlMtx);
    if (m_player.PlaybackSession().PlaybackState() ==
        winrt::Windows::Media::Playback::MediaPlaybackState::Playing)
      m_player.Pause();
  }
  {
    std::lock_guard l(m_mtx);
    m_queue.clear();
    if (m_deferral) { m_deferral.Complete(); m_deferral = nullptr; m_pendingReq = nullptr; }
  }
  CreateSource(); // an MSS cannot be flushed; a new source is the reliable "flush"
}

bool CMFSession::CanQueue()
{
  if (m_useNativeEngine)
    return m_packetSource && m_packetSource->CanQueue();
  std::lock_guard l(m_mtx);
  return m_queue.size() < kMaxQueue;
}

HRESULT CMFSession::SetSequenceHeader(const uint8_t* data, size_t size)
{
  if (!m_packetSource)
    return MF_E_NOT_INITIALIZED;
  if (!m_nativeSequenceHeader.empty())
    return S_FALSE;
  const HRESULT hr = m_packetSource->SetSequenceHeader(data, size);
  if (hr == S_OK)
    m_nativeSequenceHeader.assign(data, data + size);
  return hr;
}

void CMFSession::Push(const uint8_t* d, size_t n, double dtsUs, double ptsUs, double durUs, bool key)
{
  if (m_baseUs.load() < 0)
  {
    m_baseUs = dtsUs;
    std::string hex;
    for (size_t i = 0; i < std::min<size_t>(n, 24); ++i) hex += StringUtils::Format("{:02x} ", d[i]);
    CLog::LogF(LOGDEBUG, "first sample: {} bytes, key {}, base dts {:.0f} us, head: {}", n, key, dtsUs, hex);
  }
  const double base = m_baseUs;

  if (m_useNativeEngine)
  {
    Microsoft::WRL::ComPtr<IMFSample> sample;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateSample(sample.GetAddressOf());
    if (SUCCEEDED(hr))
      hr = MFCreateMemoryBuffer(static_cast<DWORD>(n), buffer.GetAddressOf());
    BYTE* destination = nullptr;
    DWORD maximumLength = 0;
    DWORD currentLength = 0;
    if (SUCCEEDED(hr))
      hr = buffer->Lock(&destination, &maximumLength, &currentLength);
    if (SUCCEEDED(hr))
    {
      std::memcpy(destination, d, n);
      hr = buffer->Unlock();
    }
    if (SUCCEEDED(hr))
      hr = buffer->SetCurrentLength(static_cast<DWORD>(n));
    if (SUCCEEDED(hr))
      hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr))
      hr = sample->SetSampleTime(
          static_cast<LONGLONG>(std::max(0.0, ptsUs - base) * 10.0));
    if (SUCCEEDED(hr))
      hr = sample->SetSampleDuration(static_cast<LONGLONG>(durUs * 10.0));
    if (SUCCEEDED(hr))
      hr = sample->SetUINT64(MFSampleExtension_DecodeTimestamp,
                             static_cast<UINT64>(std::max(0.0, dtsUs - base) * 10.0));
    if (SUCCEEDED(hr) && key)
      hr = sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
    if (SUCCEEDED(hr))
      hr = m_packetSource->PushSample(sample.Get());
    if (m_pushed.load() < 4)
      CLog::LogF(LOGINFO,
                 "MFDV: Queued compressed sample #{} bytes={} key={} pts={:.0f} dts={:.0f} hr={:#x}",
                 m_pushed.load() + (SUCCEEDED(hr) ? 1 : 0), n, key, ptsUs, dtsUs,
                 static_cast<uint32_t>(hr));
    if (FAILED(hr))
      CLog::LogF(LOGERROR, "MFDV: Could not queue Media Foundation sample: {:#x}",
                 static_cast<uint32_t>(hr));
    else
    {
      ++m_pushed;
      if (!m_nativeSequenceHeader.empty() && !m_nativeSourceStarted)
      {
        hr = StartNativeEngineSource();
        if (FAILED(hr))
        {
          CLog::LogF(LOGERROR, "MFDV: Deferred Media Engine source start failed: {:#x}",
                     static_cast<uint32_t>(hr));
          std::lock_guard lock(m_evtMtx);
          m_failed = true;
          m_evtCv.notify_all();
        }
      }
    }
    return;
  }

  DataWriter w;
  w.WriteBytes(array_view<const uint8_t>(d, d + n));
  auto sample = MediaStreamSample::CreateFromBuffer(
      w.DetachBuffer(), TimeSpan{static_cast<int64_t>(std::max(0.0, ptsUs - base) * 10)});
  sample.DecodeTimestamp(TimeSpan{static_cast<int64_t>(std::max(0.0, dtsUs - base) * 10)});
  sample.Duration(TimeSpan{static_cast<int64_t>(durUs * 10)});
  sample.KeyFrame(key);
  ++m_pushed;

  std::lock_guard l(m_mtx);
  if (m_deferral)
  {
    m_pendingReq.Sample(sample);
    m_deferral.Complete();
    m_deferral = nullptr;
    m_pendingReq = nullptr;
    ++m_delivered;
  }
  else
    m_queue.push_back(sample);
}

void CMFSession::AttachSurface(float srcW, float srcH)
{
  auto& host = CMFCompositionHost::Get();
  if (m_useNativeEngine)
  {
    host.ShowVideoSwapChain(m_videoSwapChain.Get());
    CLog::LogF(LOGINFO, "MFDV: Native Media Engine surface attached, source {}x{}", srcW, srcH);
    return;
  }
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
  const double rate = m_slew * m_speed / static_cast<double>(DVD_PLAYSPEED_NORMAL);
  if (m_engine)
  {
    const HRESULT hr = m_engine->SetPlaybackRate(rate);
    if (FAILED(hr))
      CLog::LogF(LOGWARNING, "Media Engine playback rate rejected: {:#x}",
                 static_cast<uint32_t>(hr));
    return;
  }
  if (!m_player)
    return;
  try
  {
    m_player.PlaybackSession().PlaybackRate(rate);
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
  if (m_engine)
  {
    if (speed <= 0 || speed > 2 * DVD_PLAYSPEED_NORMAL)
    {
      if (!m_engine->IsPaused())
        m_engine->Pause();
    }
    else
    {
      ApplyRate();
      if (m_started && m_engine->IsPaused())
        m_engine->Play();
    }
    return;
  }
  if (!m_player)
    return;
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
  if (m_engine)
  {
    std::lock_guard l(m_ctlMtx);
    const double base = m_baseUs;
    if (!m_engine || !m_videoSwapChain || base < 0 || ptsUs == m_lastPts)
      return;
    m_lastPts = ptsUs;

    if (!m_started)
    {
      ApplyRate();
      if (m_speed > 0)
        m_engine->Play();
      m_started = true;
      m_lastLog = std::chrono::steady_clock::now();
      CLog::LogF(LOGDEBUG, "MFDV: native Media Engine timeline started at {:.0f} us", ptsUs);
      return;
    }

    if (m_speed == DVD_PLAYSPEED_NORMAL)
    {
      const double engineTime = m_engine->GetCurrentTime();
      const double targetTime = (ptsUs - base + m_latencyUs) / 1000000.0;
      const double driftMs = (engineTime - targetTime) * 1000.0;
      m_slew = driftMs > 40 ? 0.97 : driftMs < -40 ? 1.03
                                                  : (std::abs(driftMs) < 10 ? 1.0 : m_slew);
      ApplyRate();
    }

    LONGLONG streamTime = 0;
    HRESULT hr = m_engine->OnVideoStreamTick(&streamTime);
    if (hr != S_OK)
      return;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> output;
    const UINT bufferIndex = m_videoSwapChain3->GetCurrentBackBufferIndex();
    hr = m_videoSwapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(output.GetAddressOf()));
    if (FAILED(hr))
      return;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    hr = m_videoSwapChain->GetDesc1(&desc);
    if (FAILED(hr))
      return;

    const MFVideoNormalizedRect sourceRect{0.0f, 0.0f, 1.0f, 1.0f};
    const RECT destinationRect{0, 0, static_cast<LONG>(desc.Width),
                               static_cast<LONG>(desc.Height)};
    const MFARGB borderColor{0, 0, 0, 0};
    hr = m_engine->TransferVideoFrame(output.Get(), &sourceRect, &destinationRect, &borderColor);
    const uint64_t nextFrame = m_rendered.load() + 1;
    if (SUCCEEDED(hr) && (nextFrame == 1 || nextFrame % 120 == 0))
    {
      const auto deviceResources = DX::DeviceResources::Get();
      D3D11_TEXTURE2D_DESC stagingDesc{};
      stagingDesc.Width = 40;
      stagingDesc.Height = 8;
      stagingDesc.MipLevels = 1;
      stagingDesc.ArraySize = 1;
      stagingDesc.Format = desc.Format;
      stagingDesc.SampleDesc.Count = 1;
      stagingDesc.Usage = D3D11_USAGE_STAGING;
      stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
      HRESULT readbackHr = deviceResources->GetD3DDevice()->CreateTexture2D(
          &stagingDesc, nullptr, staging.GetAddressOf());
      if (SUCCEEDED(readbackHr))
      {
        constexpr UINT patchSize = 8;
        const UINT xPositions[] = {desc.Width / 2 - patchSize / 2,
                                   desc.Width / 4 - patchSize / 2,
                                   3 * desc.Width / 4 - patchSize / 2,
                                   desc.Width / 4 - patchSize / 2,
                                   3 * desc.Width / 4 - patchSize / 2};
        const UINT yPositions[] = {desc.Height / 2 - patchSize / 2,
                                   desc.Height / 4 - patchSize / 2,
                                   desc.Height / 4 - patchSize / 2,
                                   3 * desc.Height / 4 - patchSize / 2,
                                   3 * desc.Height / 4 - patchSize / 2};
        const char* regionNames[] = {"center", "top-left", "top-right", "bottom-left",
                                     "bottom-right"};
        auto context = deviceResources->GetImmediateContext();
        for (UINT patch = 0; patch < 5; ++patch)
        {
          const D3D11_BOX box{xPositions[patch], yPositions[patch], 0,
                              xPositions[patch] + patchSize,
                              yPositions[patch] + patchSize, 1};
          context->CopySubresourceRegion(staging.Get(), 0, patch * patchSize, 0, 0,
                                         output.Get(), 0, &box);
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        readbackHr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(readbackHr))
        {
          for (UINT patch = 0; patch < 5; ++patch)
          {
            uint64_t red = 0;
            uint64_t green = 0;
            uint64_t blue = 0;
            uint64_t alpha = 0;
            for (UINT row = 0; row < patchSize; ++row)
            {
              const auto* pixels = reinterpret_cast<const uint32_t*>(
                  static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch) +
                                   patch * patchSize;
              for (UINT column = 0; column < patchSize; ++column)
              {
                const uint32_t pixel = pixels[column];
                red += pixel & 0x3ff;
                green += (pixel >> 10) & 0x3ff;
                blue += (pixel >> 20) & 0x3ff;
                alpha += (pixel >> 30) & 0x3;
              }
            }
            constexpr uint64_t pixelCount = patchSize * patchSize;
            CLog::LogF(LOGINFO,
                       "MFDV: Transferred frame #{} {} patch average R10G10B10A2={}/{}/{}/{}",
                       nextFrame, regionNames[patch], red / pixelCount, green / pixelCount,
                       blue / pixelCount, alpha / pixelCount);
          }
          context->Unmap(staging.Get(), 0);
        }
        else
          CLog::LogF(LOGWARNING, "MFDV: Pixel readback map failed on frame #{}: {:#x}",
                     nextFrame,
                     static_cast<uint32_t>(readbackHr));
      }
      else
        CLog::LogF(LOGWARNING, "MFDV: Pixel readback texture failed on frame #{}: {:#x}",
                   nextFrame,
                   static_cast<uint32_t>(readbackHr));
    }
    if (SUCCEEDED(hr))
      hr = m_videoSwapChain->Present(1, 0);
    if (FAILED(hr))
    {
      CLog::LogF(LOGERROR, "MFDV: frame-server transfer failed: {:#x}",
                 static_cast<uint32_t>(hr));
      return;
    }

    const uint64_t rendered = ++m_rendered;
    if (rendered == 1 || rendered % 120 == 0)
      CLog::LogF(LOGDEBUG, "MFDV: native Media Engine presented frame #{}", rendered);
    return;
  }

  if (!m_player)
    return;
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

  const int64_t want = static_cast<int64_t>((ptsUs - base + m_latencyUs) * 10);
  const double driftMs =
      (m_player.PlaybackSession().Position().count() - want) / 10000.0;
  m_slew = driftMs > 40 ? 0.97 : driftMs < -40 ? 1.03 : (std::abs(driftMs) < 10 ? 1.0 : m_slew);
  ApplyRate();

  const auto now = std::chrono::steady_clock::now();
  if (now - m_lastLog > 2s)
  {
    m_lastLog = now;
    CLog::LogF(LOGDEBUG, "drift {:.1f} ms, slew {:.2f}, pushed {}, consumed {}, rendered {}", driftMs,
               m_slew, m_pushed.load(), m_delivered.load(), m_rendered.load());
  }
}
