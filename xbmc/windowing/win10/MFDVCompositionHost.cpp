// xbmc/windowing/win10/MFDVCompositionHost.cpp
#include "MFDVCompositionHost.h"

#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Media.Playback.h>

using namespace winrt::Windows::UI::Composition;

CMFDVCompositionHost& CMFDVCompositionHost::Get()
{
  static CMFDVCompositionHost s;
  return s;
}

bool CMFDVCompositionHost::Init()
{
  if (m_target)
    return true;

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

void CMFDVCompositionHost::SetGuiSwapChain(IDXGISwapChain1* swapChain)
{
  auto interop = m_compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
  winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionSurface> abiSurface;
  winrt::check_hresult(interop->CreateCompositionSurfaceForSwapChain(swapChain, abiSurface.put()));

  auto brush = m_compositor.CreateSurfaceBrush(abiSurface.as<ICompositionSurface>());
  m_gui.Brush(brush);
}

void CMFDVCompositionHost::ShowVideo(const winrt::Windows::Media::Playback::MediaPlayerSurface& s)
{
  auto brush = m_compositor.CreateSurfaceBrush(s.CompositionSurface());
  brush.Stretch(CompositionStretch::Fill);
  m_video.Brush(brush);
  m_video.IsVisible(true);
}

void CMFDVCompositionHost::SetVideoRect(float x, float y, float w, float h)
{
  m_video.Offset({x, y, 0.f});
  m_video.Size({w, h});
}

void CMFDVCompositionHost::HideVideo()
{
  if (m_video)
    m_video.IsVisible(false);
}

void CMFDVCompositionHost::SetOutputSize(float outW, float outH, float logW, float logH)
{
  m_gui.Size({outW, outH});
  m_root.Size({outW, outH});
  m_root.Scale({logW / outW, logH / outH, 1.f});
}
