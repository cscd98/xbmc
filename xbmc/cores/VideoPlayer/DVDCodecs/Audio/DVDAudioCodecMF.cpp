/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDAudioCodecMF.h"

#include "DVDStreamInfo.h"
#include "DVDCodecs/DVDFactoryCodec.h"
#include "cores/AudioEngine/Utils/AEUtil.h"

#include <algorithm>
#include <cmath>

extern "C"
{
#include <libavcodec/avcodec.h>
}

bool CDVDAudioCodecMF::Register()
{
  CDVDFactoryCodec::RegisterHWAudioCodec(
      "mfdv", [](CProcessInfo& processInfo) { return std::make_unique<CDVDAudioCodecMF>(processInfo); });
  return true;
}

bool CDVDAudioCodecMF::Open(CDVDStreamInfo& hints, CDVDCodecOptions&)
{
  if (hints.codec != AV_CODEC_ID_AAC || hints.channels <= 0 || hints.samplerate <= 0 ||
      !hints.extradata)
    return false;

  m_session = CMFSession::Acquire();
  if (!m_session->ConfigureAudio(hints))
  {
    m_session.reset();
    return false;
  }

  m_channels = static_cast<unsigned int>(hints.channels);
  m_format.m_dataFormat = AE_FMT_S16NE;
  m_format.m_sampleRate = static_cast<unsigned int>(hints.samplerate);
  m_format.m_frameSize = m_channels * sizeof(int16_t);

  AVChannelLayout channelLayout{};
  if (hints.channellayout)
    av_channel_layout_from_mask(&channelLayout, hints.channellayout);
  if (!channelLayout.nb_channels)
    av_channel_layout_default(&channelLayout, hints.channels);
  m_format.m_channelLayout = CAEUtil::GetAEChannelLayout(channelLayout.u.mask);
  av_channel_layout_uninit(&channelLayout);

  m_lastPts = DVD_NOPTS_VALUE;
  return true;
}

void CDVDAudioCodecMF::Dispose()
{
  m_frames.clear();
  m_currentFrame = {};
  m_session.reset();
}

bool CDVDAudioCodecMF::AddData(const DemuxPacket& packet)
{
  if (!m_session || !packet.pData || packet.iSize <= 0)
    return true;
  if (!m_session->CanQueue(true))
    return false;

  double duration = packet.duration;
  if (duration <= 0)
    duration = 1024.0 * DVD_TIME_BASE / m_format.m_sampleRate;

  double dts = packet.dts;
  double pts = packet.pts;
  if (dts == DVD_NOPTS_VALUE)
    dts = pts;
  if (pts == DVD_NOPTS_VALUE)
    pts = dts;
  if (dts == DVD_NOPTS_VALUE)
    dts = pts = m_lastPts == DVD_NOPTS_VALUE ? 0.0 : m_lastPts + duration;
  m_lastPts = pts;

  m_session->PushAudio(packet.pData, packet.iSize, dts, pts, duration);

  const auto sampleCount = static_cast<unsigned int>(
      std::max(1.0, std::round(duration * m_format.m_sampleRate / DVD_TIME_BASE)));
  SilentFrame frame;
  frame.samples.resize(static_cast<size_t>(sampleCount) * m_channels, 0);
  frame.pts = pts;
  frame.duration = duration;
  frame.sampleCount = sampleCount;
  m_frames.emplace_back(std::move(frame));
  return true;
}

void CDVDAudioCodecMF::GetData(DVDAudioFrame& frame)
{
  frame.nb_frames = 0;
  if (m_frames.empty())
    return;

  m_currentFrame = std::move(m_frames.front());
  m_frames.pop_front();

  frame.data[0] = reinterpret_cast<uint8_t*>(m_currentFrame.samples.data());
  frame.pts = m_currentFrame.pts;
  frame.hasTimestamp = frame.pts != DVD_NOPTS_VALUE;
  frame.duration = m_currentFrame.duration;
  frame.nb_frames = m_currentFrame.sampleCount;
  frame.framesOut = 0;
  frame.framesize = m_channels * sizeof(int16_t);
  frame.planes = 1;
  frame.format = m_format;
  frame.bits_per_sample = 16;
  frame.passthrough = false;
  frame.audio_service_type = AV_AUDIO_SERVICE_TYPE_MAIN;
  frame.matrix_encoding = AV_MATRIX_ENCODING_NONE;
  frame.profile = 0;
  frame.hasDownmix = false;
}

void CDVDAudioCodecMF::Reset()
{
  m_frames.clear();
  m_currentFrame = {};
  m_lastPts = DVD_NOPTS_VALUE;
}