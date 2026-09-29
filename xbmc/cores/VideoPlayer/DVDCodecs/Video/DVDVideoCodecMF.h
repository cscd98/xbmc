/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once
#include "DVDVideoCodec.h"
#include "DVDStreamInfo.h"
#include "MFSession.h"
#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "utils/BitstreamConverter.h"

#include <deque>
#include <map>
#include <memory>
#include <vector>

class CMFDVVideoBuffer : public CVideoBuffer
{
public:
  explicit CMFDVVideoBuffer(int id) : CVideoBuffer(id) { m_pixFormat = AV_PIX_FMT_NONE; }
};

class CMFDVBufferPool : public IVideoBufferPool
{
public:
  CVideoBuffer* Get() override
  {
    std::unique_lock lock(m_section);
    CMFDVVideoBuffer* b;
    if (!m_free.empty()) { b = m_all[m_free.front()]; m_free.pop_front(); }
    else { b = new CMFDVVideoBuffer(static_cast<int>(m_all.size())); m_all.push_back(b); }
    b->Acquire(GetPtr());
    return b;
  }
  void Return(int id) override { std::unique_lock lock(m_section); m_free.push_back(id); }
  ~CMFDVBufferPool() override { for (auto b : m_all) delete b; }
private:
  CCriticalSection m_section;
  std::vector<CMFDVVideoBuffer*> m_all;
  std::deque<int> m_free;
};

class CDVDVideoCodecMF : public CDVDVideoCodec
{
public:
  explicit CDVDVideoCodecMF(CProcessInfo& p) : CDVDVideoCodec(p) {}
  ~CDVDVideoCodecMF() override;
  static bool Register();

  bool Open(CDVDStreamInfo& hints, CDVDCodecOptions& options) override;
  bool AddData(const DemuxPacket& packet) override;
  void Reset() override;
  void SetSpeed(int speed) override { if (m_session) m_session->SetSpeed(speed); }
  VCReturn GetPicture(VideoPicture* pic) override;
  const char* GetName() override { return "mfdv"; }
  void SetCodecControl(int flags) override { m_ctrl = flags; }

private:
  struct Pending { double pts, dts, dur; };

  std::shared_ptr<CMFSession> m_session;
  std::shared_ptr<CMFDVBufferPool> m_pool = std::make_shared<CMFDVBufferPool>();
  CDVDStreamInfo m_hints;
  std::multimap<double, Pending> m_reorder; // Kodi expects pictures in pts order
  size_t m_reorderDepth = 6;
  int m_nalLen = 4;
  int m_ctrl = 0;
  double m_lastPts = DVD_NOPTS_VALUE;
  CBitstreamConverter m_bsc;
  int m_dvPacketLogCount{0};
};
