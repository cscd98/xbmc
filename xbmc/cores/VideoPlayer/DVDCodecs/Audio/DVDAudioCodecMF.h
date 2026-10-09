/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVDAudioCodec.h"
#include "cores/VideoPlayer/DVDCodecs/Video/MFSession.h"

#include <deque>
#include <memory>
#include <vector>

class CDVDStreamInfo;

class CDVDAudioCodecMF : public CDVDAudioCodec
{
public:
  explicit CDVDAudioCodecMF(CProcessInfo& processInfo) : CDVDAudioCodec(processInfo) {}
  static bool Register();

  bool Open(CDVDStreamInfo& hints, CDVDCodecOptions& options) override;
  void Dispose() override;
  bool AddData(const DemuxPacket& packet) override;
  void GetData(DVDAudioFrame& frame) override;
  void Reset() override;
  AEAudioFormat GetFormat() override { return m_format; }
  std::string GetName() override { return "mfdv-aac"; }

private:
  struct SilentFrame
  {
    std::vector<int16_t> samples;
    double pts{DVD_NOPTS_VALUE};
    double duration{0.0};
    unsigned int sampleCount{0};
  };

  AEAudioFormat m_format;
  std::shared_ptr<CMFSession> m_session;
  std::deque<SilentFrame> m_frames;
  SilentFrame m_currentFrame;
  unsigned int m_channels{0};
  double m_lastPts{DVD_NOPTS_VALUE};
};