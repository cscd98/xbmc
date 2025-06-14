/*
 *  Copyright (C) 2005-2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecGStreamer.h"

#include "DVDCodecs/DVDCodecs.h"
#include "DVDCodecs/DVDFactoryCodec.h"
#include "DVDStreamInfo.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "cores/VideoPlayer/VideoRenderers/RenderManager.h"
#include "cores/VideoSettings.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/CPUInfo.h"
#include "utils/StringUtils.h"
#include "utils/XTimeUtils.h"
#include "utils/log.h"
#include "windowing/wayland/WinSystemWayland.h"

#ifdef TARGET_WEBOS
  #include "windowing/wayland/WinSystemWaylandWebOS.h"
#endif

extern "C" {
  #include "gstlibav/gstavcodecmap.h"
}

#include <memory>
#include <mutex>

using namespace KODI::WINDOWING::WAYLAND;

namespace
{
constexpr auto PULL_SAMPLE_TIMEOUT = 10;
} // unnamed namespace

//------------------------------------------------------------------------------
// Video Buffers
//------------------------------------------------------------------------------

class CVideoBufferGStreamer : public CVideoBuffer
{
public:
  CVideoBufferGStreamer(IVideoBufferPool &pool, int id);
  ~CVideoBufferGStreamer() override;
  void GetPlanes(uint8_t*(&planes)[YuvImage::MAX_PLANES]) override;
  void GetStrides(int(&strides)[YuvImage::MAX_PLANES]) override;

  void SetRef(GstVideoFrame *frame, GstVideoInfo *info);
  void Unref();

protected:
  GstVideoFrame* m_pFrame;
};

CVideoBufferGStreamer::CVideoBufferGStreamer(IVideoBufferPool &pool, int id)
: CVideoBuffer(id), m_pFrame{nullptr}
{
}

CVideoBufferGStreamer::~CVideoBufferGStreamer()
{
  if(m_pFrame) {
    gst_video_frame_unmap(m_pFrame);
    m_pFrame = nullptr;
  }
}

void CVideoBufferGStreamer::GetPlanes(uint8_t*(&planes)[YuvImage::MAX_PLANES])
{
  planes[0] = static_cast<uint8_t*>GST_VIDEO_FRAME_PLANE_DATA(m_pFrame, 0);
  planes[1] = static_cast<uint8_t*>GST_VIDEO_FRAME_PLANE_DATA(m_pFrame, 1);
  planes[2] = static_cast<uint8_t*>GST_VIDEO_FRAME_PLANE_DATA(m_pFrame, 2);
}

void CVideoBufferGStreamer::GetStrides(int(&strides)[YuvImage::MAX_PLANES])
{
  strides[0] = GST_VIDEO_FRAME_PLANE_STRIDE(m_pFrame, 0);
  strides[1] = GST_VIDEO_FRAME_PLANE_STRIDE(m_pFrame, 1);
  strides[2] = GST_VIDEO_FRAME_PLANE_STRIDE(m_pFrame, 2);
}

void CVideoBufferGStreamer::SetRef(GstVideoFrame *frame, GstVideoInfo *info)
{
  CLog::Log(LOGDEBUG, "CVideoBufferGStreamer::SetRef()");

  if(m_pFrame) {
    //gst_object_unref(m_pFrame);
    gst_video_frame_unmap(m_pFrame);
  }

  // create new frame
  m_pFrame = g_slice_new(GstVideoFrame);

  // map new frame
  GstBuffer *dstBuffer = gst_buffer_new_allocate(nullptr, gst_buffer_get_size(frame->buffer), nullptr);

  if (!gst_video_frame_map (m_pFrame, info, dstBuffer, GST_MAP_WRITE)) {
    gst_video_frame_unmap(frame);
    return;
  }
  
  if(!gst_video_frame_copy(m_pFrame, frame))  {
    CLog::Log(LOGERROR, "CVideoBufferGStreamer::SetRef() Unable to copy source frame to dest");
  }

  m_pixFormat = (AVPixelFormat)gst_ffmpeg_videoformat_to_pixfmt(frame->info.finfo->format);

  gst_buffer_unref(dstBuffer);
  gst_video_frame_unmap(frame);
}

void CVideoBufferGStreamer::Unref()
{
  if(m_pFrame) {
    gst_video_frame_unmap(m_pFrame);
    m_pFrame = nullptr;
  }
}

//------------------------------------------------------------------------------

class CVideoBufferPoolGStreamer : public IVideoBufferPool
{
public:
  ~CVideoBufferPoolGStreamer() override;
  void Return(int id) override;
  CVideoBuffer* Get() override;

protected:
  CCriticalSection m_critSection;
  std::vector<CVideoBufferGStreamer*> m_all;
  std::deque<int> m_used;
  std::deque<int> m_free;
};

CVideoBufferPoolGStreamer::~CVideoBufferPoolGStreamer()
{
  for (auto buf : m_all)
  {
    delete buf;
  }
}

CVideoBuffer* CVideoBufferPoolGStreamer::Get()
{
  std::unique_lock<CCriticalSection> lock(m_critSection);

  CVideoBufferGStreamer *buf = nullptr;
  if (!m_free.empty())
  {
    int idx = m_free.front();
    m_free.pop_front();
    m_used.push_back(idx);
    buf = m_all[idx];
  }
  else
  {
    int id = m_all.size();
    buf = new CVideoBufferGStreamer(*this, id);
    m_all.push_back(buf);
    m_used.push_back(id);
  }

  buf->Acquire(GetPtr());
  return buf;
}

void CVideoBufferPoolGStreamer::Return(int id)
{
  std::unique_lock<CCriticalSection> lock(m_critSection);

  m_all[id]->Unref();
  auto it = m_used.begin();
  while (it != m_used.end())
  {
    if (*it == id)
    {
      m_used.erase(it);
      break;
    }
    else
      ++it;
  }
  m_free.push_back(id);
}

CDVDVideoCodecGStreamer::CDVDVideoCodecGStreamer(CProcessInfo &processInfo)
: CDVDVideoCodec(processInfo), m_threadRunning{false},
  m_isReady{false}, m_isPlaying{false}, m_hasSample{false},
  m_needData{false},  m_videoSink{"waylandsink"}, m_hasSinkLinkedToSurface{false},
  m_currentPts{0},
  m_exportedWindowName{""},
  m_name{0}, m_pFrame{nullptr},
  m_codecControlFlags{0}, m_videoBufferPool{nullptr}
{
  data.main_loop = nullptr;
  data.bus = nullptr;
  data.pipeline = nullptr;
  data.input_caps = nullptr;
  data.app_sink = nullptr;
  data.video_sink = nullptr;
  data.app_source = nullptr;
  data.queue = nullptr;
  data.pipeline = nullptr;
  data.video_convert = nullptr;
  data.video_scale = nullptr;
  data.decoder = nullptr;
  data.videoInfo = nullptr;

  m_videoBufferPool = std::make_shared<CVideoBufferPoolGStreamer>();

  GError* error = nullptr;

  /* Initialize GStreamer */
  if(!gst_init_check(nullptr, nullptr, &error)) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer(): gst_init_check() failed: {}", error->message);
    g_error_free(error);
  }
}

CDVDVideoCodecGStreamer::~CDVDVideoCodecGStreamer()
{
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::¬CDVDVideoCodecGStreamer()");

  Stop();
}

std::unique_ptr<CDVDVideoCodec> CDVDVideoCodecGStreamer::Create(CProcessInfo& processInfo)
{
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_USEGSTREAMER))
    return std::make_unique<CDVDVideoCodecGStreamer>(processInfo);

  return nullptr;
}

void CDVDVideoCodecGStreamer::Register()
{
  CDVDFactoryCodec::RegisterHWVideoCodec("gstreamer", CDVDVideoCodecGStreamer::Create);
}

std::atomic<bool> CDVDVideoCodecGStreamer::m_InstanceGuard(false);

/*
 * Open the decoder, returns true on success
*/
bool CDVDVideoCodecGStreamer::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  // allow only 1 instance here
  if (m_InstanceGuard.exchange(true))
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::Open() - InstanceGuard locked");
    return false;
  }

  m_hints = hints;
  m_options = options;

  if (!hints.width || !hints.height)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::Open() - null width or height, cannot handle");
    m_InstanceGuard.exchange(false);
    return false;
  }
  
  CLog::Log(
      LOGDEBUG,
      "CDVDVideoCodecGStreamer::Open() hints: Width {}x Height {}, Fpsrate {} / Fpsscale {}, calculated fps {}, "
      "CodecID {}, Level {}, Profile {}, PTS_invalid {}, Tag {}, Extradata-Size: {}",
      hints.width, hints.height, hints.fpsrate, hints.fpsscale,
      static_cast<float>(m_hints.fpsrate) / m_hints.fpsscale,
      hints.codec, hints.level,
      hints.profile, hints.ptsinvalid, hints.codec_tag, hints.extradata.GetSize());
  
  // allow the use of a gstreamer video sink if requested
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_PREFERGSTREAMERVIDEOSINK))
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::Open() - using: {}", m_videoSink);

    m_videoBuffer.Reset();

    m_videoBuffer.iWidth  = m_hints.width;
    m_videoBuffer.iHeight = m_hints.height;
    m_videoBuffer.iDisplayWidth  = m_hints.width;
    m_videoBuffer.iDisplayHeight = m_hints.height;
    m_videoBuffer.stereoMode = m_hints.stereo_mode;

    m_processInfo.SetVideoDecoderName(std::string(m_videoSink), true );
    m_processInfo.SetVideoPixelFormat("Surface");
    m_processInfo.SetVideoDimensions(m_hints.width, m_hints.height);
    m_processInfo.SetVideoDeintMethod("hardware");
    m_processInfo.SetVideoDAR(m_hints.aspect);
    m_processInfo.SetVideoFps(static_cast<float>(m_hints.fpsrate) / m_hints.fpsscale);
  }

  // create a gstreamer pipeline
  if(!CreatePipeline(hints, options)) {
    m_InstanceGuard.exchange(false);
    return false;
  }

  return true;
}

bool CDVDVideoCodecGStreamer::CreatePipeline(CDVDStreamInfo &hints, CDVDCodecOptions &options) {

  // this is only needed to populate needed data for gst_ffmpeg_codecid_to_caps
  const AVCodec *codec = avcodec_find_decoder(hints.codec);

  if(!codec) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::CreatePipeline() Unable to find ffmpeg codec {}", hints.codec);
    return false;
  }

  AVCodecContext *m_pCodecContext = avcodec_alloc_context3(codec);
  if (!m_pCodecContext)
    return false;

  m_pCodecContext->height = hints.height;
  m_pCodecContext->width = hints.width;
  m_pCodecContext->framerate.den = hints.fpsscale;
  m_pCodecContext->framerate.num = hints.fpsrate;
  m_pCodecContext->codec_tag = hints.codec_tag;
  m_pCodecContext->bits_per_coded_sample = hints.bitsperpixel;
  m_pCodecContext->bits_per_raw_sample = hints.bitdepth;
  m_pCodecContext->flags = hints.flags;

  // channels, channel_layout are depreciated
  m_pCodecContext->sample_rate = hints.samplerate;
  m_pCodecContext->bit_rate = hints.bitrate;
  m_pCodecContext->block_align = hints.blockalign;

  // pix_fmt only needed for raw video?
  //m_pCodecContext->pix_fmt = ??

  // codec_data is contained in hints.extradata
  if (hints.extradata && hints.extradata.GetSize() > 0)
  {
    m_pCodecContext->extradata_size = hints.extradata.GetSize();
    m_pCodecContext->extradata =
        static_cast<uint8_t*>(av_mallocz(hints.extradata.GetSize() + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(m_pCodecContext->extradata, hints.extradata.GetData(), hints.extradata.GetSize());
  }

  data.input_caps = gst_ffmpeg_codecid_to_caps(hints.codec, m_pCodecContext, true);

  avcodec_free_context(&m_pCodecContext);

  gchar *input_caps_char = gst_caps_to_string(data.input_caps);

  GError *error = nullptr;
  std::string pipeline = "appsrc"
                        + std::string(" caps=\"") + std::string(input_caps_char) + "\" name=video_src"
                        + " ! decodebin name=my_decoder" // sink-caps
                        + " ! videoconvert name=video_convert"
                        + " ! videoscale name=video_scale"
                        + " ! queue name=my_queue";

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_PREFERGSTREAMERVIDEOSINK)) {
    pipeline += " ! " + m_videoSink;

    if(!getenv("WAYLAND_DISPLAY")) {
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::ExportWindow() - please set WAYLAND_DISPLAY first");
      return false;
    }

    pipeline += " display=" + std::string(getenv("WAYLAND_DISPLAY"));
    pipeline += " name=video_sink";
  } else {
    pipeline += " ! appsink sync=false max-buffers=2 name=app_sink";
  }

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::CreatePipeline(): pipeline {}", pipeline);

  data.pipeline = gst_parse_launch(pipeline.c_str(), &error);
  
  if (!data.pipeline) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::CreatePipeline() - Unable to create pipeline: {}", error->message);
    return false;
  }

  bool autoPlug = false;
  if (pipeline.find("decodebin") != std::string::npos) {
    autoPlug = true;
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::CreatePipeline() - autoPlug enabled");
  }

  data.app_source = gst_bin_get_by_name(GST_BIN(data.pipeline), "video_src");
  data.decoder = gst_bin_get_by_name(GST_BIN(data.pipeline), "my_decoder");
  data.video_convert = gst_bin_get_by_name(GST_BIN(data.pipeline), "video_convert");
  data.video_scale = gst_bin_get_by_name(GST_BIN(data.pipeline), "video_scale");
  data.queue = gst_bin_get_by_name(GST_BIN(data.pipeline), "my_queue");
  data.bus = gst_pipeline_get_bus(GST_PIPELINE(data.pipeline));

  // listen for messages
  gst_bus_add_watch(data.bus, (GstBusFunc)CBBusMessage, this);

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_PREFERGSTREAMERVIDEOSINK))
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::Open() - validating sink: {}", m_videoSink);

    data.video_sink = gst_bin_get_by_name(GST_BIN(data.pipeline), "video_sink");

    if(!data.video_sink) {
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::CreatePipeline(): no videosink");
      return false;
    }

    // export a window for the sink to render to
    ExportWindow();

    // listen for flushes
    GstPad* pad = gst_element_get_static_pad(data.video_sink, "sink");
    gst_pad_add_probe(pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                      EventProbe, this, nullptr);
    gst_object_unref(pad);
  }
  else
  {
    data.app_sink = gst_bin_get_by_name(GST_BIN(data.pipeline), "app_sink");

    if(!data.app_sink) {
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::CreatePipeline() - No appsink");
      return false;
    }

    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::CreatePipeline() - setting sink to emit signals");
    g_object_set(data.app_sink, "emit-signals", true, nullptr);
    //g_signal_connect (data.app_sink, "need-data", G_CALLBACK (CBNeedData), this);
    g_signal_connect(data.app_sink, "new-sample", G_CALLBACK (CBNewSample), this);
  }

  /*
    stream-type:
      GST_APP_STREAM_TYPE_STREAM (0) – No seeking is supported in the stream, such as a live stream. (PUSH)
      GST_APP_STREAM_TYPE_SEEKABLE (1) – The stream is seekable but seeking might not be very fast, such as data from a webserver. (PUSH)
      GST_APP_STREAM_TYPE_RANDOM_ACCESS (2) – The stream is seekable and seeking is fast, such as in a local file. (PULL)
  */
  g_object_set(G_OBJECT(data.app_source),
              "stream-type", 1, // stream-type is stream or seekable for push mode
              "format", GST_FORMAT_TIME, // GST_FORMAT_TIME (3) for timestamped buffers
              "is-live", true,
              nullptr);

  if(!data.app_source) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::CreatePipeline() - No appsrc");
    return false;
  }

  g_signal_connect(data.app_source, "seek-data", G_CALLBACK (CBSeekData), this);

  if(autoPlug) {
    g_signal_connect (data.decoder, "autoplug-select", G_CALLBACK (CBAutoPlugSelect), this);
  }

  if(!StartMessageThread()) {
    return false;
  }

  // if we are not using auto-plugging then playback should not have to wait for m_isReady
  if (!autoPlug) {
    m_isReady = true;
  }

  return true;
}

bool CDVDVideoCodecGStreamer::ExportWindow() {

  auto match = VideoSinkFromString(m_videoSink);

  if(match == std::nullopt)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::ExportWindow() - the sink specificied is not supported for exporting a window");
    return false;
  }

  auto waylandSocket = "/run/user/1000/" + std::string(getenv("WAYLAND_DISPLAY"));

#ifdef TARGET_WEBOS
  waylandSocket = "/tmp/xdg/" + std::string(getenv("WAYLAND_DISPLAY"));
  auto winSystem = static_cast<CWinSystemWaylandWebOS*>(GetWinSystem());
  bool supportsExportedWindow = winSystem->SupportsExportedWindow();
#else
  auto winSystem = GetWinSystem();
  bool supportsExportedWindow = true;
#endif

  if(!supportsExportedWindow) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::ExportWindow() - exported window is not supported!");
    return false;
  }

  g_object_set(G_OBJECT(data.video_sink), "display", waylandSocket, nullptr);

  // tell waylandsink which display to connect to
  struct wl_display* wlDisplay = winSystem->GetDisplay();

  if(!wlDisplay) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::ExportWindow() - could not get wl_display!");
    return false;
  }

  GstContext *context = gst_context_new("GstWaylandDisplayHandleContextType", true);
  gst_structure_set(gst_context_writable_structure(context),
                    "display", G_TYPE_POINTER, wlDisplay, nullptr);
  
  // Push context to sink
  gst_element_set_context(data.video_sink, context);
  gst_context_unref(context);

  // tell waylandsink which surface to render to
  if (!GST_IS_VIDEO_OVERLAY(data.video_sink)) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::ExportWindow() - sink does not support GstVideoOverlay interface");
  }

  // set a wait for a message back if we can wire up the video sink to the display
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::ExportWindow() - video sink is a overlay, requesting linkeage");

  //gst_bus_add_watch(data.bus, (GstBusFunc)BusSyncHandler, this);
  gst_bus_set_sync_handler(data.bus, BusSyncHandler, this, nullptr);

  return true;
}

bool CDVDVideoCodecGStreamer::SetState(GstState state) {

  GstStateChangeReturn ret;

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::SetState()");
  ret = gst_element_set_state(data.pipeline, state);

  switch(ret) {
    case GST_STATE_CHANGE_FAILURE:
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::SetState() - GST_STATE_CHANGE_FAILURE, returned false");
      return false;
    case GST_STATE_CHANGE_NO_PREROLL:
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::SetState() - GST_STATE_CHANGE_NO_PREROLL");
      break;
    default:
      break;
  }

  return true;
}

bool CDVDVideoCodecGStreamer::StartMessageThread() {

    if (data.main_loop) {
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::StartMessageThread() - loop already started");
      return false;
    }

    SetState(GST_STATE_PLAYING);

    data.main_loop = g_main_loop_new(nullptr, false);

    m_threadRunning = true;
    m_thread = std::thread([&]() {
        g_main_loop_run(data.main_loop);
    });

    return true;
}

// add data, decoder has to consume the entire packet
bool CDVDVideoCodecGStreamer::AddData(const DemuxPacket &packet)
{
  if (!packet.pData) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::AddData() - no packet data");
    return true;
  }

  //if (CServiceBroker::GetLogging().CanLogComponent(LOGVIDEO)) {
    CLog::Log(LOGDEBUG,
                "CDVDVideoCodecGStreamer::AddData() - packet stream:{} dts:{:0.2f} pts:{:0.2f} duration:{} state:{}",
                packet.iStreamId, packet.dts, packet.pts, packet.duration, m_state);
  //}

  if (!m_threadRunning) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::AddData() - thread not running");
    return true;
  }

  /*if(!m_needData) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::AddData() - Do not need data");
    return false;
  }*/

  // pipeline now ready, but we are not in a playing state
  /*if(m_isReady && !m_isPlaying) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::AddData() - pipeline is ready but we are not PLAYING");
    return true;
  }*/

  auto pts = (packet.pts == DVD_NOPTS_VALUE)
              ? GST_CLOCK_TIME_NONE
              : static_cast<int64_t>(packet.pts / DVD_TIME_BASE * AV_TIME_BASE);

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_PREFERGSTREAMERVIDEOSINK)
        && !m_hasSinkLinkedToSurface) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::AddData() - pipleline not ready - surface not linked - calc. pts: {}", pts);

    // check we haven't already let one frame through as below
    if(m_isReady) {
      return true;
    }
  }

  // first frame allow through so gstreamer will auto-plug to finish setting up the pipeline
  if(!m_isReady && pts > 0) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::AddData() - pipleline not ready - calc. pts: {}", pts);
    return true;
  }

  // DVD_NOPTS_VALUE = 18442240474082181120
  // GST_CLOCK_TIME_NONE = 18446744073709551615
  // First packet.dts / DVD_TIME_BASE * AV_TIME_BASE) = -9223372036854775808
  auto dts = (packet.dts == DVD_NOPTS_VALUE)
              ? GST_CLOCK_TIME_NONE
             : static_cast<int64_t>(packet.dts / DVD_TIME_BASE * AV_TIME_BASE);

  GstFlowReturn ret;

  CLog::Log(LOGDEBUG,
                "CDVDVideoCodecGStreamer::AddData() - used dts:{} used pts:{}", dts, pts);

  // correct bytes size of a frame (width * height * bytes per pixel)
  GstBuffer * buffer = gst_buffer_new_allocate(nullptr, packet.iSize, nullptr);
  gst_buffer_fill(buffer, 0, packet.pData, packet.iSize);

  GST_BUFFER_DTS      (buffer) = dts;
  GST_BUFFER_PTS      (buffer) = pts;
  GST_BUFFER_DURATION (buffer) = packet.duration;

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::AddData() buffer dts {} pts {}",
    buffer->dts, buffer->pts);

  /* Push the buffer into the appsrc */
  g_signal_emit_by_name(data.app_source, "push-buffer", buffer, &ret);
  //ret = gst_app_src_push_buffer(GST_APP_SRC(data.app_source), buffer);
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::AddData() pushing buffer");

  /* Free the buffer now that we are done with it */
  gst_buffer_unref(buffer);

  if (ret != GST_FLOW_OK) {
    /* We got some error, stop sending data */
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::AddData() - pushing the buffer failed");
    Stop();
    return false;
  }

  return true;
}

void CDVDVideoCodecGStreamer::SetCodecControl(int flags) {
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::SetCodecControl() {}", flags);

  if (m_codecControlFlags != flags)
  {
    CLog::Log(LOGDEBUG, LOGVIDEO, "CDVDVideoCodecGStreamer::{} {:x}->{:x}", __func__,
              m_codecControlFlags, flags);

    m_codecControlFlags = flags;
  }
}

// GetPicture controls decoding
CDVDVideoCodec::VCReturn CDVDVideoCodecGStreamer::GetPicture(VideoPicture* pVideoPicture)
{
  if(!m_isReady) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - pipeline not ready yet");
    return VC_NONE;
  }

  // no sample produced yet
  /*if(!HasSample()) {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - no sample yet");
      return VC_BUFFER;
  }*/

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_PREFERGSTREAMERVIDEOSINK)) {

    if(!m_hasSinkLinkedToSurface) {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - surface not linked yet");
      return VC_NONE;
    }

    if (m_state == StreamState::ERROR)
      return VC_ERROR;

    if (m_state == StreamState::EOS)
      return VC_EOF;

    if (m_state == StreamState::FLUSHED) // || !m_pFrame)
      return VC_BUFFER;

    //GstSample *lastSample = nullptr;

    /*g_object_get(G_OBJECT(data.video_sink), "last-sample", &lastSample, nullptr);

    if (lastSample) {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - we have a last-sample!");
    } else {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - no last-sample!");
    }*/

    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - using sink: {}", m_videoSink);

#ifdef TARGET_WEBOS
    GObjectClass* klass = G_OBJECT_GET_CLASS(data.video_sink);
    if (g_object_class_find_property(klass, "current-pts") != nullptr)
    {
      // we must render directly to a surface as decoded frames are never exported
      unsigned long currentPts = -1;

      g_object_get(data.video_sink, "current-pts", &currentPts, nullptr);

      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - m_currentPts = {} currentPts = {}", m_currentPts, currentPts);

      // queue more data
      if (currentPts == m_currentPts) {
        CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - queing more data");
        return VC_BUFFER;
      }

      m_currentPts = currentPts;
    }
#endif

    if (m_videoBuffer.videoBuffer)
        m_videoBuffer.videoBuffer->Release();

    m_videoBuffer.videoBuffer = m_videoBufferPool->Get();

    pVideoPicture->videoBuffer = nullptr;
    pVideoPicture->SetParams(m_videoBuffer);
    pVideoPicture->videoBuffer = m_videoBuffer.videoBuffer;
    pVideoPicture->dts = 0;
    pVideoPicture->pts = m_currentPts;

    m_videoBuffer.videoBuffer = nullptr;

    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - returning a picture");

    return VC_PICTURE;
  }

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - pulling sample");

  GstSample *sample = nullptr;

  // pull-preroll -> last preroll sample in appsink. This was the sample that caused the appsink to preroll in the PAUSED state.
  // pull-sample ->  blocks until a sample or EOS becomes available or the appsink element is set to the READY/NULL state.
  /*if(lastSample) {
    sample = lastSample;
  } else {*/
    sample = gst_app_sink_try_pull_sample((GstAppSink *)data.app_sink, PULL_SAMPLE_TIMEOUT); // optional timeout
  //}

  if(!sample) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - no sample");
    return VC_BUFFER;
  }

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - have a sample!");
  GstBuffer *buffer;

  buffer = gst_sample_get_buffer(sample);

  if(!buffer) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::GetPicture() - could not get get buffer from sample");
    gst_sample_unref(sample);
    return VC_ERROR;
  }

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - we have a buffer!!");

  GstMapInfo mapInfo;

  // Fills info with the GstMapInfo of all merged memory blocks in buffer
  if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::GetPicture() - could not map buffer from sample");
    gst_sample_unref(sample);
    return VC_ERROR;
  }

  // this only needs to be done once
  if(!data.videoInfo) {
    GstCaps *caps = gst_sample_get_caps(sample);

    if (!caps) {
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::GetPicture() - could not get caps from sample");
      gst_sample_unref(sample);
      return VC_ERROR;
    }

    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - caps: {}",
              gst_caps_to_string(caps));

    data.videoInfo = gst_video_info_new();

    if (!gst_video_info_from_caps(data.videoInfo, caps)) {
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::GetPicture() - cannot get video info from caps");
      gst_buffer_unmap(buffer, &mapInfo);
      gst_sample_unref(sample);
      return VC_ERROR;
    }

    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - info.size: {} video_info size: {} width: {} height: {} negotiated format: {}",
                        mapInfo.size, data.videoInfo->size, data.videoInfo->width, data.videoInfo->height,
                        data.videoInfo->finfo->name);
  }

  m_pFrame = g_slice_new(GstVideoFrame);

  if (!gst_video_frame_map(m_pFrame, data.videoInfo, buffer, GST_MAP_READ)) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::GetPicture() - cannot map video frame");
    gst_buffer_unmap(buffer, &mapInfo);
    gst_sample_unref(sample);
    gst_video_frame_unmap(m_pFrame);
    m_pFrame = nullptr;
    return VC_ERROR;
  }

  gst_buffer_unmap(buffer, &mapInfo);
  gst_sample_unref(sample);

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - returning a frame!!");

  SetPictureParams(pVideoPicture);

  if (pVideoPicture->videoBuffer)
  {
    pVideoPicture->videoBuffer->Release();
    pVideoPicture->videoBuffer = nullptr;
  }

  CVideoBufferGStreamer* videoBuffer =
        dynamic_cast<CVideoBufferGStreamer*>(m_videoBufferPool->Get());
  videoBuffer->SetRef(m_pFrame, data.videoInfo);
  pVideoPicture->videoBuffer = videoBuffer;

  if (!pVideoPicture->videoBuffer)
  {
    //CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer::{} - videoBuffer is null");
    if(m_pFrame) {
      gst_video_frame_unmap(m_pFrame);
      m_pFrame = nullptr;
    }
    return VC_ERROR;
  }

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::GetPicture() - returning VC_PICTURE");

  return VC_PICTURE;
}

void CDVDVideoCodecGStreamer::SetPictureParams(VideoPicture* pVideoPicture)
{
  GstVideoInfo info = m_pFrame->info;

  pVideoPicture->iWidth = info.width;
  pVideoPicture->iHeight = info.height;

  double aspect_ratio = 0;
  AVRational pixel_aspect = { info.par_n, info.par_d };
  if (pixel_aspect.num)
    aspect_ratio = av_q2d(pixel_aspect) * pVideoPicture->iWidth / pVideoPicture->iHeight;

  if (aspect_ratio <= 0.0)
    aspect_ratio =
        static_cast<double>(pVideoPicture->iWidth) / static_cast<double>(pVideoPicture->iHeight);

  if (m_DAR != aspect_ratio)
  {
    m_DAR = aspect_ratio;
    m_processInfo.SetVideoDAR(static_cast<float>(m_DAR));
  }

  pVideoPicture->iDisplayWidth =
      (static_cast<int>(lrint(pVideoPicture->iHeight * aspect_ratio))) & -3;
  pVideoPicture->iDisplayHeight = pVideoPicture->iHeight;
  if (pVideoPicture->iDisplayWidth > pVideoPicture->iWidth)
  {
    pVideoPicture->iDisplayWidth = pVideoPicture->iWidth;
    pVideoPicture->iDisplayHeight =
        (static_cast<int>(lrint(pVideoPicture->iWidth / aspect_ratio))) & -3;
  }

  pVideoPicture->color_range =
      info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255 || info.finfo->format == GST_VIDEO_FORMAT_I420 ||
      info.finfo->format == GST_VIDEO_FORMAT_Y42B || info.finfo->format == GST_VIDEO_FORMAT_Y444 ||
      m_hints.colorRange == AVCOL_RANGE_JPEG;

  auto colourPrimaries = static_cast<AVColorPrimaries>(gst_video_color_primaries_to_iso(m_pFrame->info.colorimetry.primaries));
  auto colorTransfer = static_cast<AVColorTransferCharacteristic>(gst_video_transfer_function_to_iso(info.colorimetry.transfer));

  pVideoPicture->color_primaries =  colourPrimaries == AVCOL_PRI_UNSPECIFIED
                                    ? m_hints.colorPrimaries
                                    : colourPrimaries;

  pVideoPicture->color_transfer =   colorTransfer == AVCOL_TRC_UNSPECIFIED
                                    ? m_hints.colorTransferCharacteristic
                                    : colorTransfer;

  auto colorSpace = static_cast<AVColorSpace>(gst_video_color_matrix_to_iso(info.colorimetry.matrix));

  pVideoPicture->color_space =
      colorSpace == AVCOL_SPC_UNSPECIFIED
      ? m_hints.colorSpace : colorSpace;

  pVideoPicture->chroma_position = static_cast<AVChromaLocation>(m_pFrame->info.chroma_site);

  pVideoPicture->colorBits = info.finfo->bits;

  pVideoPicture->hasDisplayMetadata = false;
  pVideoPicture->hasLightMetadata = false;

  GstCaps *caps = gst_video_info_to_caps((const GstVideoInfo *)&info);

  if(caps) {
    GstStructure *in_s = gst_caps_get_structure(caps, 0);
    if(gst_structure_has_field (in_s, "mastering-display-info")) {

      GstVideoMasteringDisplayInfo minfo;
      gst_video_mastering_display_info_from_caps(&minfo, caps);

      AVMasteringDisplayMetadata meta;
      const int chroma_den = 50000;
      const int luma_den = 10000;
      const int mapping[3] = {2, 0, 1};
      long unsigned int i;

      for (i = 0; i < G_N_ELEMENTS(meta.display_primaries); i++) {
        const int j = mapping[i];
        meta.display_primaries[i][0] = av_make_q(minfo.display_primaries[j].x, chroma_den);
        meta.display_primaries[i][1] = av_make_q(minfo.display_primaries[j].y, chroma_den);
      }

      meta.white_point[0] = av_make_q(minfo.white_point.x, chroma_den);
      meta.white_point[1] = av_make_q(minfo.white_point.y, chroma_den);
      meta.max_luminance = av_make_q(minfo.max_display_mastering_luminance, luma_den);
      meta.min_luminance = av_make_q(minfo.min_display_mastering_luminance, luma_den);

      pVideoPicture->displayMetadata = *(AVMasteringDisplayMetadata *)&meta;
      pVideoPicture->hasDisplayMetadata = true;
    }
    else if (m_hints.masteringMetadata) {
      pVideoPicture->displayMetadata = *m_hints.masteringMetadata.get();
      pVideoPicture->hasDisplayMetadata = true;
    }

    if (gst_structure_has_field (in_s, "content-light-level")) {
      GstVideoContentLightLevel linfo;
      gst_video_content_light_level_from_caps(&linfo, caps);

      AVContentLightMetadata lightMeta;
      lightMeta.MaxCLL = linfo.max_content_light_level;
      lightMeta.MaxFALL = linfo.max_frame_average_light_level;
      pVideoPicture->lightMetadata = *(AVContentLightMetadata *)&lightMeta;
      pVideoPicture->hasLightMetadata = true;
    }
    else if (m_hints.contentLightMetadata) {
      pVideoPicture->lightMetadata = *m_hints.contentLightMetadata.get();
      pVideoPicture->hasLightMetadata = true;
    }

    gst_caps_unref(caps);
  }

  pVideoPicture->iRepeatPicture = 0;
  pVideoPicture->iFlags = 0;
  pVideoPicture->iFlags |= GST_VIDEO_FRAME_IS_INTERLACED(m_pFrame) ? DVP_FLAG_INTERLACED : 0;
  pVideoPicture->iFlags |= GST_VIDEO_FRAME_IS_TOP_FIELD(m_pFrame) ? DVP_FLAG_TOP_FIELD_FIRST : 0;
  pVideoPicture->iFlags |= GST_VIDEO_FRAME_PLANE_DATA(m_pFrame, 0) ? 0 : DVP_FLAG_DROPPED;

  int64_t pts = m_pFrame->buffer->pts;

  pVideoPicture->pts = (pts == AV_NOPTS_VALUE)
                           ? GST_CLOCK_TIME_NONE
                           : static_cast<double>(pts) * DVD_TIME_BASE / AV_TIME_BASE;
  pVideoPicture->dts = GST_CLOCK_TIME_NONE;

  m_processInfo.SetVideoPixelFormat(info.finfo->name);
  m_processInfo.SetVideoDimensions(m_hints.width, m_hints.height);
  m_processInfo.SetVideoDeintMethod(gst_video_interlace_mode_to_string(info.interlace_mode));
}

// Reset the decoder
void CDVDVideoCodecGStreamer::Reset() {

  if(!data.pipeline) {
    return;
  }

  if(gst_element_send_event(GST_ELEMENT(data.pipeline), gst_event_new_flush_start())) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: Reset() - unable to start flushing");
  }

  if(gst_element_send_event(GST_ELEMENT(data.pipeline), gst_event_new_flush_stop(false))) {
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: Reset() - unable to stop flushing");
  }

  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(
        CSettings::SETTING_VIDEOPLAYER_PREFERGSTREAMERVIDEOSINK)) {
          m_videoBuffer.pts = DVD_NOPTS_VALUE;
  }

  m_state = StreamState::FLUSHED;
}

void CDVDVideoCodecGStreamer::Stop()
{
  if(m_isReady && data.app_source) {
    m_isReady = false;
    //gst_app_src_end_of_stream((GstAppSrc*)data.app_source);
  }

  if(data.main_loop) {
    g_main_loop_quit(data.main_loop);
  }

  if(m_threadRunning) {
    if(m_thread.joinable()) {
      m_thread.join();
    }
    m_threadRunning = false;
  }

  Dispose();

  m_InstanceGuard.exchange(false);
}

void CDVDVideoCodecGStreamer::Dispose()
{
  if(data.main_loop) {
    g_main_loop_unref(data.main_loop);
    data.main_loop = nullptr;
  }

  if(data.bus) {
    gst_object_unref(data.bus);
    data.bus = nullptr;
  }

  if(data.pipeline) {
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    data.pipeline = nullptr;
  }

  if(data.app_sink) gst_object_unref(data.app_sink);
  if(data.video_sink) gst_object_unref(data.video_sink);
  if(data.app_source) gst_object_unref(data.app_source);
  if(data.queue) gst_object_unref(data.queue);
  if(data.video_convert) gst_object_unref(data.video_convert);
  if(data.video_scale) gst_object_unref(data.video_scale);
  if(data.decoder) gst_object_unref(data.decoder);
  if(data.videoInfo) gst_object_unref(data.videoInfo);

  if(data.input_caps) {
    gst_caps_unref(data.input_caps);
    data.input_caps = nullptr;
  }

  data.app_sink = nullptr;
  data.video_sink = nullptr;
  data.app_source = nullptr;
  data.queue = nullptr;
  data.video_convert = nullptr;
  data.video_scale = nullptr;
  data.decoder = nullptr;
  data.videoInfo = nullptr;

  m_currentPts = 0;
  m_codecControlFlags = 0;

  if(m_pFrame) {
    gst_video_frame_unmap(m_pFrame);
    m_pFrame = nullptr;
  }

  m_hasSinkLinkedToSurface = false;
}

gboolean CDVDVideoCodecGStreamer::CBBusMessage(GstBus *bus, GstMessage *message, gpointer data)
{
  CDVDVideoCodecGStreamer *gwPtr = static_cast<CDVDVideoCodecGStreamer*>(data);

  GstMessageType messageType = GST_MESSAGE_TYPE(message);
  GError *err = nullptr;
  gchar *debug = nullptr;

  switch (messageType) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(message, &err, &debug);
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer: CBBusMessage() Received error from element {}, {}", GST_OBJECT_NAME (message->src), err->message);
      CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer: CBBusMessage() Debug info {}", (debug) ? debug : "none");
      g_error_free(err);
      g_free(debug);
      gwPtr->SetIsReady(false);
      gwPtr->m_state = StreamState::ERROR;
      gwPtr->Stop();
      break;
    case GST_MESSAGE_WARNING: {
      gst_message_parse_warning(message, &err, &debug);
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBBusMessage() received warning {}", err->message);
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_QOS: {
      // Extract the timestamps and live status from the QoS message
      gboolean live;
      guint64 running_time, stream_time, timestamp, duration;

      gst_message_parse_qos(message, &live, &running_time, &stream_time, &timestamp, &duration);

      CLog::Log(LOGDEBUG, "QoS message: live={}, rt={} st={} ts={} dur={}",
                live, running_time, stream_time, timestamp, duration);
      }
      break;
    case GST_MESSAGE_EOS: {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBBusMessage() received EOS");
      gwPtr->m_state = StreamState::EOS;
      gwPtr->SetIsReady(false);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (gwPtr->GetData().pipeline)) {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed (message, &old_state, &new_state, &pending_state);
        if(new_state == GST_STATE_PLAYING) {
          gwPtr->SetIsPlaying(true);
        } else {
          gwPtr->SetIsPlaying(false);
        }
        CLog::Log(LOGDEBUG, "Pipeline state changed from {} to {}",
          gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
      }
      break;
    }
    default:
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBBusMessage() received: {}", gst_message_type_get_name(messageType));
      break;
  }

  return true;
}

GstFlowReturn CDVDVideoCodecGStreamer::CBNeedData(GstElement *object, guint arg0, gpointer user_data) {
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBNeedData() Pipeline needs Data!");
  CDVDVideoCodecGStreamer *wrapper = static_cast<CDVDVideoCodecGStreamer *>(user_data);
  wrapper->SetNeedData();
  return GST_FLOW_OK;
}

/* The appsink has received a buffer */
GstFlowReturn CDVDVideoCodecGStreamer::CBNewSample(GstElement *object, gpointer user_data) {
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBNewSample() Pipeline has a new sample!");
  CDVDVideoCodecGStreamer *wrapper = static_cast<CDVDVideoCodecGStreamer *>(user_data);
  wrapper->SetHasSample(true);
  return GST_FLOW_OK;
}

/* called when appsrc wants us to return data from a new position with the next
 * call to push-buffer. */
bool CDVDVideoCodecGStreamer::CBSeekData(GstElement *appsrc, guint64 position, gpointer user_data)
{
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBSeekData() Seek to offset {}", position);
  CDVDVideoCodecGStreamer *wrapper = static_cast<CDVDVideoCodecGStreamer *>(user_data);
  wrapper->SetCurrentPts(position);

  return true;
}

// TODO: CWinSystemWayland? or CWinSystemWaylandWebOS
CWinSystemWayland* CDVDVideoCodecGStreamer::GetWinSystem()
{
#ifdef TARGET_WEBOS
  auto winSystem = static_cast<CWinSystemWaylandWebOS*>(CServiceBroker::GetWinSystem());
#else
  auto winSystem = static_cast<CWinSystemWayland*>(CServiceBroker::GetWinSystem());
#endif

  return winSystem;
}

GstBusSyncReply CDVDVideoCodecGStreamer::BusSyncHandler(GstBus *bus, GstMessage *message, gpointer user_data)
{
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: BusSyncHandler() - window handle message for: {}",
    G_OBJECT_TYPE_NAME(GST_MESSAGE_SRC(message)));

  if(!gst_is_video_overlay_prepare_window_handle_message(message))
  {
    return GST_BUS_PASS;
  }

  if(!G_IS_OBJECT(GST_MESSAGE_SRC(message)) ||
     !GST_IS_VIDEO_OVERLAY(GST_MESSAGE_SRC(message))) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer: BusSyncHandler() - message is not an overlay");
    return GST_BUS_PASS;
  }

  if(!user_data) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer: BusSyncHandler() - user_data is missing");
    return GST_BUS_PASS;
  }

  GstVideoOverlay* overlay = GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message));
  auto* context = static_cast<CDVDVideoCodecGStreamer *>(user_data);

  const GType overlay_type = G_OBJECT_TYPE(GST_MESSAGE_SRC(message));
  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: BusSyncHandler() - Overlay type: {}", g_type_name(overlay_type));

  auto surfaceHandle = context->GetWinSystem()->GetMainSurface();
  auto* wlSurface = surfaceHandle.c_ptr();

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: BusSyncHandler() - Before setting handle: {}", static_cast<void*>(wlSurface));
  gst_video_overlay_set_window_handle(overlay, reinterpret_cast<guintptr>(wlSurface));

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: BusSyncHandler() - setting window size");
  gst_video_overlay_set_render_rectangle(overlay, 0, 0, 1920, 1080);

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: BusSyncHandler() - setting sink linked to surface");

  context->SetHasSinkLinkedToSurface(true);

  CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: BusSyncHandler() - after setting sink linked to surface");

  //gst_message_unref(message);

  return GST_BUS_DROP;
}

GstFlowReturn CDVDVideoCodecGStreamer::CBAutoPlugSelect(GstElement *bin, GstPad *pad, GstCaps *caps, GstElementFactory *factory,
  gpointer udata) {
  CDVDVideoCodecGStreamer *wrapper = static_cast<CDVDVideoCodecGStreamer *>(udata);

  if (!GST_IS_ELEMENT_FACTORY(factory)) {
    CLog::Log(LOGERROR, "CDVDVideoCodecGStreamer: CBAutoPlugSelect() auto-plugging failed as not factory element");
    return GST_FLOW_ERROR;
  }

  std::string name = std::string("gs-") + gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
  bool useHardware = gst_element_factory_list_is_type(factory, GST_ELEMENT_FACTORY_TYPE_HARDWARE);
  bool isDecoder = gst_element_factory_list_is_type(factory, GST_ELEMENT_FACTORY_TYPE_DECODER);

  CLog::Log(LOGINFO, "CDVDVideoCodecGStreamer: CBAutoPlugSelect() auto-plugging {}: {}, {} ({})",
    (isDecoder ? "detected a decoder" : "detected"),
    gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
    gst_pb_utils_get_decoder_description(caps),
    (useHardware ? "H/W" : "S/W")
  );

  if(isDecoder) {
    wrapper->setName(name);
    wrapper->GetProcessInfo()->SetVideoDecoderName(name, useHardware);

    // now that we have a pipeline decoder, allow AddData() and GetPicture() to execute
    CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer: CBAutoPlugSelect() decoder, setting isReady");
    wrapper->SetIsReady(true);
  }

  return GST_FLOW_OK;
}

GstPadProbeReturn CDVDVideoCodecGStreamer::EventProbe(GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
  {
    auto* context = static_cast<CDVDVideoCodecGStreamer *>(user_data);

    GstEvent* event = gst_pad_probe_info_get_event(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_START)
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::EventProbe() - Flush start event detected on pad");
      // Handle flush start: pause decoding or reset state
    else if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP) {
      CLog::Log(LOGDEBUG, "CDVDVideoCodecGStreamer::EventProbe() - Flush stop event detected on pad");
      // Handle flush stop: resume decoding or reconfigure pipeline
      context->m_state = StreamState::FLUSHED;
    }
  }
  return GST_PAD_PROBE_PASS;
}
