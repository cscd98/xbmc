#pragma once
#include <dxgi1_5.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.UI.Composition.h>

class CMFDVCompositionHost
{
public:
  static CMFDVCompositionHost& Get();

  bool Init();
  bool IsActive() const { return m_target != nullptr; }
  winrt::Windows::UI::Composition::Compositor GetCompositor() const { return m_compositor; }

  void SetGuiSwapChain(IDXGISwapChain1* swapChain);
  void ShowVideo(const winrt::Windows::Media::Playback::MediaPlayerSurface& surface);
  void SetVideoRect(float x, float y, float w, float h);
  void HideVideo();

private:
  winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
  winrt::Windows::UI::Composition::CompositionTarget m_target{nullptr};
  winrt::Windows::UI::Composition::ContainerVisual m_root{nullptr};
  winrt::Windows::UI::Composition::SpriteVisual m_video{nullptr};
  winrt::Windows::UI::Composition::SpriteVisual m_gui{nullptr};
};
