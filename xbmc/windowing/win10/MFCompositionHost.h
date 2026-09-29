/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>

class CMFCompositionHost
{
public:
  static CMFCompositionHost& Get();

  bool Init();
  bool IsActive() const { return m_target != nullptr; }
  winrt::Windows::UI::Composition::Compositor GetCompositor() const { return m_compositor; }

  void SetGuiSwapChain(IDXGISwapChain1* swapChain);
  void ClearGuiSwapChainHdrMetaData();
  void ShowVideo(const winrt::Windows::Media::Playback::MediaPlayerSurface& surface);
  void SetVideoRect(float x, float y, float w, float h);
  void HideVideo();
  void SetOutputSize(float outW, float outH, float logW, float logH);
  void UpdateScale();
  bool SetDolbyVisionOutput(bool enabled);

private:
  winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
  winrt::Windows::UI::Composition::CompositionTarget m_target{nullptr};
  winrt::Windows::UI::Composition::ContainerVisual m_root{nullptr};
  winrt::Windows::UI::Composition::SpriteVisual m_video{nullptr};
  winrt::Windows::UI::Composition::SpriteVisual m_gui{nullptr};
  Microsoft::WRL::ComPtr<IDXGISwapChain4> m_guiSwapChain;
winrt::Windows::UI::Core::CoreWindow m_window{nullptr};
  float m_outW{0}, m_outH{0};
  bool m_dolbyVisionPreviousHdrState{false};
};
