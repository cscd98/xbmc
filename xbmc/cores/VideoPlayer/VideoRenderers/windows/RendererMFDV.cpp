// RendererMFDV.cpp
#include "RendererMFDV.h"

#include "DVDCodecs/Video/DVDVideoCodecMFDV.h"
#include "VideoRenderers/RenderFactory.h"
#include "VideoRenderers/RenderFlags.h"
#include "settings/MediaSettings.h"

CBaseRenderer* CRendererMFDV::Create(CVideoBuffer* buffer)
{
  return dynamic_cast<CMFDVVideoBuffer*>(buffer) ? new CRendererMFDV() : nullptr;
}

bool CRendererMFDV::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("mfdv", CRendererMFDV::Create);
  return true;
}

bool CRendererMFDV::Configure(const VideoPicture& picture, float, unsigned orientation)
{
  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_renderOrientation = orientation;
  CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
  SetViewMode(m_videoSettings.m_ViewMode);

  m_session = CMFDVSession::Acquire();
  m_session->AttachSurface(static_cast<float>(m_sourceWidth), static_cast<float>(m_sourceHeight));
  return true;
}

void CRendererMFDV::UnInit()
{
  m_session.reset(); // the codec's Close() hides the visual
  m_configured = false;
}

void CRendererMFDV::RenderUpdate(int index, int, bool, unsigned, unsigned)
{
  m_configured = true;
  ManageRenderArea();
  m_session->SetDestRect(m_destRect.x1, m_destRect.y1, m_destRect.Width(), m_destRect.Height());
  m_session->OnFrame(m_pts[index]);
}

bool CRendererMFDV::Supports(ERENDERFEATURE f) const
{
  return f == RENDERFEATURE_ZOOM || f == RENDERFEATURE_STRETCH || f == RENDERFEATURE_PIXEL_RATIO ||
         f == RENDERFEATURE_VERTICAL_SHIFT;
}
