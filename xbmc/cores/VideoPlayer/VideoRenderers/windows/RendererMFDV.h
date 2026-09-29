// xbmc/cores/VideoPlayer/VideoRenderers/windows/RendererMFDV.h
#pragma once
#include "cores/VideoPlayer/VideoRenderers/BaseRenderer.h"
#include "DVDCodecs/Video/MFDVSession.h"

class CRendererMFDV : public CBaseRenderer
{
public:
  ~CRendererMFDV() override { UnInit(); }
  static CBaseRenderer* Create(CVideoBuffer* buffer);
  static bool Register();

  bool Configure(const VideoPicture& picture, float fps, unsigned orientation) override;
  bool IsConfigured() override { return m_configured; }
  bool ConfigChanged(const VideoPicture&) override { return false; }
  CRenderInfo GetRenderInfo() override { CRenderInfo i; i.max_buffer_size = 4; return i; }
  void AddVideoPicture(const VideoPicture& pic, int index) override { m_pts[index % 4] = pic.pts; }
  void ReleaseBuffer(int) override {}
  void UnInit() override;
  void Update() override {}
  void RenderUpdate(int index, int index2, bool clear, unsigned flags, unsigned alpha) override;
  bool SupportsMultiPassRendering() override { return false; }
  bool IsGuiLayer() override { return false; }
  bool Supports(ESCALINGMETHOD) const override { return false; }
  bool Supports(ERENDERFEATURE f) const override;
bool VideoBypassesFramebuffer() const override { return true; } // match the exact signature in BaseRenderer.h
void AddVideoPicture(const VideoPicture& pic, int index) override { m_pts[index] = pic.pts; }

private:
  std::shared_ptr<CMFDVSession> m_session;
  bool m_configured{false};
  double m_pts[NUM_BUFFERS]{};
};
