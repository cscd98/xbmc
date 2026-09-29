/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MFCompositionHost.h"

#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Media.Playback.h>

// start for set hdmi
#include <cmath>
#include "utils/SystemInfo.h"
#include "utils/log.h"
#include "platform/win10/AsyncHelpers.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.System.Profile.h>
// end

#include "utils/log.h"

using namespace winrt::Windows::UI::Composition;

// hdmi
using namespace winrt::Windows::Foundation::Metadata;
using namespace winrt::Windows::Graphics::Display::Core;
using namespace winrt::Windows::Graphics::Display;
// end hdmi

CMFCompositionHost& CMFCompositionHost::Get()
{
  static CMFCompositionHost s;
  return s;
}

bool CMFCompositionHost::Init()
{
  if (m_target)
    return true;

  m_window = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
  m_compositor = Compositor();
  m_target = m_compositor.CreateTargetForCurrentView();
  m_root = m_compositor.CreateContainerVisual();
  m_target.Root(m_root);

  m_video = m_compositor.CreateSpriteVisual(); // bottom
  m_video.IsVisible(false);
  m_gui = m_compositor.CreateSpriteVisual(); // top
  m_root.Children().InsertAtBottom(m_video);
  m_root.Children().InsertAtTop(m_gui);
  return true;
}

void CMFCompositionHost::SetGuiSwapChain(IDXGISwapChain1* swapChain)
{
  auto interop = m_compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();

  winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionSurface> abiSurface;
  winrt::check_hresult(
      interop->CreateCompositionSurfaceForSwapChain(swapChain, abiSurface.put()));

  auto brush = m_compositor.CreateSurfaceBrush(
      abiSurface.as<ICompositionSurface>());
  m_gui.Brush(brush);

  //winrt::com_ptr<IDXGISwapChain3> swapChain3;
  // winrt::check_hresult(
  //    swapChain->QueryInterface(IID_PPV_ARGS(swapChain3.put())));

  //swapChain3->SetColorSpace1(
  //    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
}

void CMFCompositionHost::ShowVideo(const winrt::Windows::Media::Playback::MediaPlayerSurface& s)
{
  CLog::LogF(LOGDEBUG,
    "MFDV composition: showing MediaPlayerSurface");

  auto brush = m_compositor.CreateSurfaceBrush(s.CompositionSurface());
  brush.Stretch(CompositionStretch::Fill);
  m_video.Brush(brush);
  m_video.IsVisible(true);
}

void CMFCompositionHost::SetVideoRect(float x, float y, float w, float h)
{
  m_video.Offset({x, y, 0.0f});
  m_video.Size({w, h});
}

void CMFCompositionHost::HideVideo()
{
  if (m_video)
    m_video.IsVisible(false);
}

void CMFCompositionHost::SetOutputSize(float outW, float outH, float logW, float logH)
{
  m_outW = outW;
  m_outH = outH;
  m_gui.Size({outW, outH});
  m_root.Size({outW, outH});
  UpdateScale();
}

void CMFCompositionHost::UpdateScale()
{
  if (!m_window || m_outW <= 0 || m_outH <= 0) return;
  auto apply = [this]
  {
    const auto b = m_window.Bounds();
    m_root.Scale({b.Width / m_outW, b.Height / m_outH, 1.f});
  };
  if (m_window.Dispatcher().HasThreadAccess()) apply();
  else m_window.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, apply);
}

bool CMFCompositionHost::SetDolbyVisionOutput(bool enabled)
{
  CLog::LogF(LOGINFO, "MFDV: SetDolbyVisionOutput({})", enabled);

  if (CSysInfo::GetWindowsDeviceFamily() != CSysInfo::WindowsDeviceFamily::Xbox)
  {
    CLog::LogF(LOGWARNING, "MFDV: Not running on Xbox");
    return false;
  }

  const bool dvPropertySupported =
      ApiInformation::IsPropertyPresent(
          L"Windows.Graphics.Display.Core.HdmiDisplayMode",
          L"IsDolbyVisionLowLatencySupported");

  CLog::LogF(LOGINFO, "MFDV: IsDolbyVisionLowLatencySupported property: {}",
             dvPropertySupported);

  if (!dvPropertySupported)
    return false;

  const auto hdmiInfo = HdmiDisplayInformation::GetForCurrentView();
  if (!hdmiInfo)
  {
    CLog::LogF(LOGWARNING, "MFDV: HdmiDisplayInformation unavailable");
    return false;
  }

  const auto currentMode = hdmiInfo.GetCurrentDisplayMode();

  CLog::LogF(
      LOGINFO,
      "MFDV: Current HDMI mode: {}x{} @ {:.6f}Hz, DV LL: {}, SMPTE2084: {}",
      currentMode.ResolutionWidthInRawPixels(),
      currentMode.ResolutionHeightInRawPixels(),
      currentMode.RefreshRate(),
      currentMode.IsDolbyVisionLowLatencySupported(),
      currentMode.IsSmpte2084Supported());

  const auto modes = hdmiInfo.GetSupportedDisplayModes();

  CLog::LogF(LOGINFO, "MFDV: Supported HDMI modes: {}", modes.Size());

  for (const auto& mode : modes)
  {
    CLog::LogF(
        LOGINFO,
        "MFDV: Mode: {}x{} @ {:.6f}Hz, DV LL: {}, SMPTE2084: {}",
        mode.ResolutionWidthInRawPixels(),
        mode.ResolutionHeightInRawPixels(),
        mode.RefreshRate(),
        mode.IsDolbyVisionLowLatencySupported(),
        mode.IsSmpte2084Supported());

    if (mode.ResolutionWidthInRawPixels() != currentMode.ResolutionWidthInRawPixels() ||
        mode.ResolutionHeightInRawPixels() != currentMode.ResolutionHeightInRawPixels() ||
        fabs(mode.RefreshRate() - currentMode.RefreshRate()) > 0.00001)
    {
      continue;
    }

    if (enabled && !mode.IsDolbyVisionLowLatencySupported())
    {
      CLog::LogF(LOGINFO, "MFDV: Matching mode is not Dolby Vision capable");
      continue;
    }

    const auto hdrOption =
        enabled
            ? HdmiDisplayHdrOption::DolbyVisionLowLatency
            : (m_dolbyVisionPreviousHdrState ? HdmiDisplayHdrOption::Eotf2084
                                             : HdmiDisplayHdrOption::None);

    CLog::LogF(
        LOGINFO,
        "MFDV: Requesting {} for {}x{} @ {:.6f}Hz",
        enabled ? "DolbyVisionLowLatency" :
                  (m_dolbyVisionPreviousHdrState ? "Eotf2084" : "None"),
        mode.ResolutionWidthInRawPixels(),
        mode.ResolutionHeightInRawPixels(),
        mode.RefreshRate());

    const bool success =
        Wait(hdmiInfo.RequestSetCurrentDisplayModeAsync(mode, hdrOption));

    CLog::LogF(LOGINFO, "MFDV: RequestSetCurrentDisplayModeAsync result: {}",
               success);

    if (success)
    {
      m_dolbyVisionPreviousHdrState =
          enabled && currentMode.IsSmpte2084Supported();

      const auto newMode = hdmiInfo.GetCurrentDisplayMode();

      CLog::LogF(
          LOGINFO,
          "MFDV: New HDMI mode: {}x{} @ {:.6f}Hz, DV LL: {}, SMPTE2084: {}",
          newMode.ResolutionWidthInRawPixels(),
          newMode.ResolutionHeightInRawPixels(),
          newMode.RefreshRate(),
          newMode.IsDolbyVisionLowLatencySupported(),
          newMode.IsSmpte2084Supported());

      CLog::LogF(LOGINFO, "MFDV: SetDolbyVisionOutput success: {}",
                 success);
    }

    return success;
  }

  CLog::LogF(LOGWARNING,
             "MFDV: No matching HDMI mode found for {}x{} @ {:.6f}Hz{}",
             currentMode.ResolutionWidthInRawPixels(),
             currentMode.ResolutionHeightInRawPixels(),
             currentMode.RefreshRate(),
             enabled ? " with Dolby Vision LL support" : "");

  return false;
}
