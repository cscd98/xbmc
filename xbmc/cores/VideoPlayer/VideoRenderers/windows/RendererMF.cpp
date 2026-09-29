#include "RendererMF.h"

#include "DVDCodecs/Video/DVDVideoCodecMF.h"
#include "VideoRenderers/RenderFactory.h"
#include "VideoRenderers/RenderFlags.h"
#include "settings/MediaSettings.h"

#include "utils/log.h"

CBaseRenderer* CRendererMF::Create(CVideoBuffer* buffer)
{
  return dynamic_cast<CMFDVVideoBuffer*>(buffer) ? new CRendererMF() : nullptr;
}

bool CRendererMF::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("mfdv", CRendererMF::Create);
  return true;
}

bool CRendererMF::Configure(const VideoPicture& picture, float, unsigned orientation)
{
  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_renderOrientation = orientation;
  CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
  SetViewMode(m_videoSettings.m_ViewMode);

  m_session = CMFSession::Acquire();
  m_session->AttachSurface(static_cast<float>(m_sourceWidth), static_cast<float>(m_sourceHeight));
  return true;
}

void CRendererMF::UnInit()
{
  m_session.reset(); // the codec's Close() hides the visual
  m_configured = false;
}

void CRendererMF::RenderUpdate(int index, int, bool, unsigned, unsigned)
{
  m_configured = true;
  ManageRenderArea();
  m_session->SetDestRect(m_destRect.x1, m_destRect.y1, m_destRect.Width(), m_destRect.Height());
  if (m_valid[index])            // index 0 is a never-filled placeholder until the first frame
    m_session->OnFrame(m_pts[index]);
}

bool CRendererMF::Supports(ERENDERFEATURE f) const
{
  return f == RENDERFEATURE_ZOOM || f == RENDERFEATURE_STRETCH || f == RENDERFEATURE_PIXEL_RATIO ||
         f == RENDERFEATURE_VERTICAL_SHIFT;
}
