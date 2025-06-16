/*
 *  Copyright (C) 2005-2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/DVDCodecs/DVDCodecs.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "DVDVideoCodec.h"
#include "windowing/wayland/WinSystemWayland.h"

#include <string>
#include <thread>

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/pbutils/descriptions.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _GstPipelineData {
  GstElement  *pipeline, *app_source;
  GstElement  *queue;
  GstElement  *decoder;
  GstElement  *video_convert, *video_scale;
  GstElement  *app_sink, *video_sink;
  GstCaps     *input_caps;
  GstVideoInfo *videoInfo;

  GstBus *bus;
  GMainLoop  *main_loop;

} GstPipelineData;

enum class StreamState
{
  RESET,
  FLUSHED,
  READY,
  RUNNING,
  EOS,
  ERROR
};

enum class VideoSinks
{
  AUTO_VIDEO_SINK,
  WAYLAND_VIDEO_SINK,
  LX_VIDEO_SINK
};

class CDVDVideoCodecGStreamer : public CDVDVideoCodec
{
public:
  explicit CDVDVideoCodecGStreamer(CProcessInfo &processInfo);
  ~CDVDVideoCodecGStreamer() override;

  static std::unique_ptr<CDVDVideoCodec> Create(CProcessInfo& processInfo);
  static void Register();

  bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options) override;
  bool AddData(const DemuxPacket &packet) override;
  void SetPictureParams(VideoPicture* pVideoPicture);
  CDVDVideoCodec::VCReturn GetPicture(VideoPicture* pVideoPicture) override;
  const char* GetName() override { return m_name.c_str(); };
  void setName(std::string newName) { m_name = newName; };
  void Reset() override;
  void Stop();
  void Dispose();
  void SetCodecControl(int flags) override;

protected:
  bool CreatePipeline(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  bool StartMessageThread();

  void SetNeedData() { m_needData = true; };
  GstPipelineData GetData() { return data; };
  bool IsThreadRunning() { return m_threadRunning; };
  bool HasSample() { return m_hasSample; }
  void SetHasSample(bool hasSample) { m_hasSample = hasSample; };
  void SetIsPlaying(bool isPlaying) { m_isPlaying = isPlaying; };
  void SetHasSinkLinkedToSurface(bool hasSinkLinkedToSurface) { m_hasSinkLinkedToSurface = hasSinkLinkedToSurface; };
  void SetIsReady(bool isReady) { m_isReady = isReady; };
  CProcessInfo *GetProcessInfo() { return &m_processInfo; };

  static gboolean CBBusMessage(GstBus *bus, GstMessage *message, gpointer data);
  static GstFlowReturn CBNeedData(GstElement *object, guint arg0, gpointer user_data);
  static GstFlowReturn CBNewSample(GstElement *object, gpointer user_data);
  static GstFlowReturn CBAutoPlugSelect(GstElement *bin, GstPad *pad, GstCaps *caps, GstElementFactory *factory, gpointer udata);
  static GstBusSyncReply BusSyncHandler(GstBus* bus, GstMessage *message, gpointer user_data);

  KODI::WINDOWING::WAYLAND::CWinSystemWayland* GetWinSystem();

  bool ExportWindow();
  static bool CBSeekData(GstElement * appsrc, guint64 position, gpointer user_data);
  void SetCurrentPts(unsigned long newPts) { m_currentPts = newPts; };
  GstState GetState();
  bool SetState(GstState state);
  static GstPadProbeReturn EventProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
  static GstPadProbeReturn AutoPlugProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);
  static void OnDecoderPadAdded(GstElement* element, GstPad* pad, gpointer user_data);
  static GstPadProbeReturn FirstBufferProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

  inline std::optional<VideoSinks> VideoSinkFromString(const std::string& sinkStr)
  {
    if (sinkStr == "waylandsink")     return VideoSinks::WAYLAND_VIDEO_SINK;
    if (sinkStr == "lxvideosink")     return VideoSinks::LX_VIDEO_SINK;
    if (sinkStr == "autovideosink")   return VideoSinks::AUTO_VIDEO_SINK;
    return std::nullopt;
  }

  std::string VideoSinkToString(VideoSinks sink)
  {
    switch(sink)
    {
      case VideoSinks::AUTO_VIDEO_SINK:    return "autovideosink";
      case VideoSinks::WAYLAND_VIDEO_SINK: return "waylandsink";
      case VideoSinks::LX_VIDEO_SINK:      return "lxvideosink";
      default:                             return "";
    }
  }

  StreamState m_state{StreamState::FLUSHED};

private:
  GstPipelineData data;

  std::thread m_thread;
  std::mutex m_loopMutex;
  bool m_threadRunning;
  bool m_firstFrameSent;
  bool m_isReady;
  bool m_isPlaying;
  bool m_hasSample;
  bool m_needData;

  std::string m_videoSink;
  bool m_hasSinkLinkedToSurface;
  unsigned long m_currentPts;
  std::string m_exportedWindowName;

  std::string m_name;
  CDVDStreamInfo m_hints;
  CDVDCodecOptions m_options;
  
  GstVideoFrame *m_pFrame;
  int m_codecControlFlags;
  double m_DAR = 1.0;
  VideoPicture m_videoBuffer;
  std::shared_ptr<IVideoBufferPool> m_videoBufferPool;

  bool m_preferVideoSink;

  static std::atomic<bool> m_InstanceGuard;

  GstBuffer *m_lastBuffer;
};
