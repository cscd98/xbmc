// xbmc/cores/VideoPlayer/VideoRenderers/windows/RendererMF.h
#pragma once
#include "cores/VideoPlayer/VideoRenderers/BaseRenderer.h"
#include "DVDCodecs/Video/DVDVideoCodec.h"
#include "DVDCodecs/Video/MFSession.h"

class CRendererMF : public CBaseRenderer
{
public:
  ~CRendererMF() override { UnInit(); }
  static CBaseRenderer* Create(CVideoBuffer* buffer);
  static bool Register();

  bool Configure(const VideoPicture& picture, float fps, unsigned orientation) override;
  bool IsConfigured() override { return m_configured; }
  bool ConfigChanged(const VideoPicture&) override { return false; }
  CRenderInfo GetRenderInfo() override { CRenderInfo i; i.max_buffer_size = 4; return i; }
  void AddVideoPicture(const VideoPicture& p, int i) override { m_pts[i] = p.pts; m_valid[i] = true; }
  void ReleaseBuffer(int i) override { m_valid[i] = false; }
  bool Flush(bool) override { std::fill(std::begin(m_valid), std::end(m_valid), false); return false; }
  void UnInit() override;
  void Update() override {}
  void RenderUpdate(int index, int index2, bool clear, unsigned flags, unsigned alpha) override;
  bool SupportsMultiPassRendering() override { return false; }
  bool IsGuiLayer() override { return false; }
  bool Supports(ESCALINGMETHOD) const override { return false; }
  bool Supports(ERENDERFEATURE f) const override;
  bool VideoBypassesFramebuffer() override { return true; }

private:
  std::shared_ptr<CMFSession> m_session;
  bool m_configured{false};
  double m_pts[NUM_BUFFERS]{};
  bool m_valid[NUM_BUFFERS]{};
};
