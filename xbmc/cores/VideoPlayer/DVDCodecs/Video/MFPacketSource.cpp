/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MFPacketSource.h"

#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <propvarutil.h>

#include <algorithm>
#include <atomic>
#include <cwchar>

#include "utils/log.h"

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

constexpr size_t kMaxSamples = 12;
constexpr wchar_t kSourceUrl[] = L"kodi-mfdv://video";
constexpr GUID CLSID_DV_RENDERER_CATEGORY{0x145cd8b4, 0x92f4, 0x4b23,
                      {0x8a, 0xe7, 0xe0, 0xdf, 0x06, 0xc2, 0xda, 0x95}};
constexpr GUID kMftEnumVideoRendererExtensionProfile{
  0x62c56928, 0x9a4e, 0x443b, {0xb9, 0xdc, 0xca, 0xc8, 0x30, 0xc2, 0x41, 0x00}};

class CMFPacketStream final
  : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFMediaStream>
{
public:
  HRESULT Initialize(CMFPacketSource* source, IMFStreamDescriptor* descriptor)
  {
    m_source = source;
    m_descriptor = descriptor;
    return MFCreateEventQueue(&m_eventQueue);
  }

  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override
  {
    return m_eventQueue ? m_eventQueue->GetEvent(flags, event) : MF_E_SHUTDOWN;
  }

  STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override
  {
    return m_eventQueue ? m_eventQueue->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
  }

  STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override
  {
    return m_eventQueue ? m_eventQueue->EndGetEvent(result, event) : MF_E_SHUTDOWN;
  }

  STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                          const PROPVARIANT* value) override
  {
    return m_eventQueue ? m_eventQueue->QueueEventParamVar(type, extendedType, status, value)
                        : MF_E_SHUTDOWN;
  }

  STDMETHODIMP GetMediaSource(IMFMediaSource** source) override
  {
    if (!source)
      return E_POINTER;
    *source = nullptr;
    if (!m_source)
      return MF_E_SHUTDOWN;
    return m_source->QueryInterface(IID_PPV_ARGS(source));
  }

  STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override
  {
    if (!descriptor)
      return E_POINTER;
    *descriptor = nullptr;
    return m_descriptor ? m_descriptor.CopyTo(descriptor) : MF_E_SHUTDOWN;
  }

  STDMETHODIMP RequestSample(IUnknown* token) override
  {
    const uint64_t request = ++m_requests;
    const HRESULT hr = m_source ? m_source->RequestSample(token, this) : MF_E_SHUTDOWN;
    if (request <= 4)
      CLog::LogF(LOGINFO, "MFDV: Stream RequestSample #{} token={} hr={:#x}", request,
                 token != nullptr, static_cast<uint32_t>(hr));
    return hr;
  }

  HRESULT Deliver(IMFSample* sample, IUnknown* token)
  {
    if (token)
    {
      const HRESULT hr = sample->SetUnknown(MFSampleExtension_Token, token);
      if (FAILED(hr))
        return hr;
    }
    return m_eventQueue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample);
  }

  HRESULT QueueSimpleEvent(MediaEventType type, const PROPVARIANT* value = nullptr)
  {
    return m_eventQueue ? m_eventQueue->QueueEventParamVar(type, GUID_NULL, S_OK, value)
                        : MF_E_SHUTDOWN;
  }

  void Shutdown()
  {
    if (m_eventQueue)
    {
      m_eventQueue->Shutdown();
      m_eventQueue.Reset();
    }
    m_source = nullptr;
    m_descriptor.Reset();
  }

private:
  CMFPacketSource* m_source{nullptr};
  ComPtr<IMFStreamDescriptor> m_descriptor;
  ComPtr<IMFMediaEventQueue> m_eventQueue;
  std::atomic<uint64_t> m_requests{0};
};

class CMFMediaEngineExtension final
  : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFMediaEngineExtension>
{
public:
  explicit CMFMediaEngineExtension(IMFMediaSource* source) : m_source(source) {}

  STDMETHODIMP CanPlayType(BOOL, BSTR, MF_MEDIA_ENGINE_CANPLAY* answer) override
  {
    if (!answer)
      return E_POINTER;
    *answer = MF_MEDIA_ENGINE_CANPLAY_MAYBE;
    return S_OK;
  }

  STDMETHODIMP BeginCreateObject(BSTR url, IMFByteStream*, MF_OBJECT_TYPE type,
                                 IUnknown** cancelCookie, IMFAsyncCallback* callback,
                                 IUnknown* state) override
  {
    if (!callback)
      return E_POINTER;
    if (cancelCookie)
      *cancelCookie = nullptr;
    CLog::LogF(LOGINFO, "MFDV: Extension BeginCreateObject url='{}' type={}",
               url ? winrt::to_string(winrt::hstring(url)) : "<null>",
               static_cast<unsigned>(type));
    if (!url || _wcsnicmp(url, L"kodi-mfdv:", 10) != 0 ||
      type != MF_OBJECT_MEDIASOURCE)
    {
      CLog::LogF(LOGDEBUG, "MFDV: Extension rejected source request (type {}, url match {})",
                 static_cast<unsigned>(type),
                 url && _wcsnicmp(url, L"kodi-mfdv:", 10) == 0);
      return MF_E_UNSUPPORTED_BYTESTREAM_TYPE;
    }
    if (!m_source)
      return MF_E_SHUTDOWN;

    ComPtr<IMFAsyncResult> result;
    HRESULT hr = MFCreateAsyncResult(m_source.Get(), callback, state, &result);
    if (FAILED(hr))
      return hr;
    hr = MFInvokeCallback(result.Get());
    CLog::LogF(FAILED(hr) ? LOGERROR : LOGINFO,
           "MFDV: Extension callback dispatch returned {:#x}",
           static_cast<uint32_t>(hr));
    return hr;
  }

  STDMETHODIMP CancelObjectCreation(IUnknown*) override { return S_OK; }

  STDMETHODIMP EndCreateObject(IMFAsyncResult* result, IUnknown** object) override
  {
    if (!result || !object)
      return E_POINTER;
    *object = nullptr;
    const HRESULT hr = result->GetObject(object);
    CLog::LogF(LOGINFO, "MFDV: Extension EndCreateObject returned {:#x}",
               static_cast<uint32_t>(hr));
    return hr;
  }

private:
  ComPtr<IMFMediaSource> m_source;
};

HRESULT ActivateDolbyVisionP5RendererEffect(IMFTransform** transform)
{
  if (!transform)
    return E_POINTER;
  *transform = nullptr;

  IMFActivate** activations = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFTEnumEx(CLSID_DV_RENDERER_CATEGORY, MFT_ENUM_FLAG_SORTANDFILTER, nullptr,
                         nullptr, &activations, &count);
  if (FAILED(hr))
    return hr;

  HRESULT result = MF_E_TOPO_CODEC_NOT_FOUND;
  for (UINT32 i = 0; i < count; ++i)
  {
    PROPVARIANT value;
    PropVariantInit(&value);
    hr = activations[i]->GetItem(kMftEnumVideoRendererExtensionProfile, &value);
    bool matches = false;
    if (SUCCEEDED(hr) && value.vt == (VT_VECTOR | VT_LPWSTR))
    {
      for (ULONG j = 0; j < value.calpwstr.cElems; ++j)
      {
        const wchar_t* profile = value.calpwstr.pElems[j];
        if (profile && _wcsicmp(profile, L"dvhe.05") == 0)
        {
          matches = true;
          break;
        }
      }
    }
    PropVariantClear(&value);
    if (!matches)
      continue;

    hr = activations[i]->ActivateObject(IID_PPV_ARGS(transform));
    if (SUCCEEDED(hr))
    {
      result = S_OK;
      break;
    }
    result = hr;
  }

  for (UINT32 i = 0; i < count; ++i)
    activations[i]->Release();
  CoTaskMemFree(activations);
  return result;
}

HRESULT CMFPacketSource::Initialize(unsigned width, unsigned height, unsigned fpsRate,
                                    unsigned fpsScale, const uint8_t* sequenceHeader,
                                    size_t sequenceHeaderSize)
{
  if (width == 0 || height == 0)
    return E_INVALIDARG;

  m_width = width;
  m_height = height;
  m_fpsRate = fpsRate;
  m_fpsScale = fpsScale;
  if (sequenceHeader && sequenceHeaderSize)
  {
    m_sequenceHeader.assign(sequenceHeader, sequenceHeader + sequenceHeaderSize);
    const HRESULT hr = CreateStreamDescriptorLocked();
    m_descriptorStatus = hr;
    m_descriptorReady = true;
    if (FAILED(hr))
      return hr;
  }
  return MFCreateEventQueue(&m_eventQueue);
}

HRESULT CMFPacketSource::CreateStreamDescriptorLocked()
{
  ComPtr<IMFMediaType> mediaType;
  HRESULT hr = MFCreateMediaType(&mediaType);
  if (FAILED(hr))
    return hr;
  hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr))
    hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_HEVC);
  if (SUCCEEDED(hr))
    hr = mediaType->SetUINT64(MF_MT_FRAME_SIZE, Pack2UINT32AsUINT64(m_width, m_height));
  if (SUCCEEDED(hr) && m_fpsRate && m_fpsScale)
    hr = mediaType->SetUINT64(MF_MT_FRAME_RATE,
                              Pack2UINT32AsUINT64(m_fpsRate, m_fpsScale));
  if (SUCCEEDED(hr))
    hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (SUCCEEDED(hr) && !m_sequenceHeader.empty())
    hr = mediaType->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, m_sequenceHeader.data(),
                            static_cast<UINT32>(m_sequenceHeader.size()));
  if (FAILED(hr))
    return hr;

  IMFMediaType* mediaTypes[] = {mediaType.Get()};
  return MFCreateStreamDescriptor(0, 1, mediaTypes, &m_streamDescriptor);
}

HRESULT CMFPacketSource::SetSequenceHeader(const uint8_t* data, size_t size)
{
  if (!data || size == 0)
    return E_INVALIDARG;

  std::lock_guard lock(m_mutex);
  if (m_shutdown)
    return MF_E_SHUTDOWN;
  if (m_descriptorReady)
    return m_descriptorStatus;

  m_sequenceHeader.assign(data, data + size);
  m_descriptorStatus = CreateStreamDescriptorLocked();
  m_descriptorReady = true;
  CLog::LogF(FAILED(m_descriptorStatus) ? LOGERROR : LOGINFO,
             "MFDV: Set HEVC sequence header ({} bytes), descriptor result {:#x}", size,
             static_cast<uint32_t>(m_descriptorStatus));
  return m_descriptorStatus;
}

HRESULT CMFPacketSource::CreateEngineExtension(IMFMediaEngineExtension** extension)
{
  if (!extension)
    return E_POINTER;
  *extension = nullptr;
  ComPtr<CMFMediaEngineExtension> created = Make<CMFMediaEngineExtension>(this);
  if (!created)
    return E_OUTOFMEMORY;
  return created.CopyTo(extension);
}

bool CMFPacketSource::CanQueue()
{
  std::lock_guard lock(m_mutex);
  return !m_shutdown && m_samples.size() + m_tokens.size() < kMaxSamples;
}

HRESULT CMFPacketSource::PushSample(IMFSample* sample)
{
  if (!sample)
    return E_POINTER;

  ComPtr<IMFMediaStream> stream;
  ComPtr<IUnknown> token;
  {
    std::lock_guard lock(m_mutex);
    if (m_shutdown)
      return MF_E_SHUTDOWN;
    if (!m_tokens.empty())
    {
      token = std::move(m_tokens.front());
      m_tokens.pop_front();
      stream = m_stream;
    }
    else
    {
      if (m_samples.size() >= kMaxSamples)
        return MF_E_NOTACCEPTING;
      m_samples.emplace_back(sample);
      return S_OK;
    }
  }
  if (!stream)
    return MF_E_SHUTDOWN;
  return static_cast<CMFPacketStream*>(stream.Get())->Deliver(sample, token.Get());
}

HRESULT CMFPacketSource::RequestSample(IUnknown* token, CMFPacketStream* stream)
{
  ComPtr<IMFSample> sample;
  {
    std::lock_guard lock(m_mutex);
    if (m_shutdown)
      return MF_E_SHUTDOWN;
    if (!m_samples.empty())
    {
      sample = std::move(m_samples.front());
      m_samples.pop_front();
    }
    else
    {
      m_tokens.emplace_back(token);
      m_stream = stream;
      return S_OK;
    }
  }
  return stream->Deliver(sample.Get(), token);
}

STDMETHODIMP CMFPacketSource::GetEvent(DWORD flags, IMFMediaEvent** event)
{
  return m_eventQueue ? m_eventQueue->GetEvent(flags, event) : MF_E_SHUTDOWN;
}

STDMETHODIMP CMFPacketSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
{
  return m_eventQueue ? m_eventQueue->BeginGetEvent(callback, state) : MF_E_SHUTDOWN;
}

STDMETHODIMP CMFPacketSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
{
  return m_eventQueue ? m_eventQueue->EndGetEvent(result, event) : MF_E_SHUTDOWN;
}

STDMETHODIMP CMFPacketSource::QueueEvent(MediaEventType type, REFGUID extendedType,
                                         HRESULT status, const PROPVARIANT* value)
{
  return m_eventQueue ? m_eventQueue->QueueEventParamVar(type, extendedType, status, value)
                      : MF_E_SHUTDOWN;
}

STDMETHODIMP CMFPacketSource::GetCharacteristics(DWORD* characteristics)
{
  if (!characteristics)
    return E_POINTER;
  *characteristics = MFMEDIASOURCE_IS_LIVE | MFMEDIASOURCE_CAN_PAUSE;
  return S_OK;
}

STDMETHODIMP CMFPacketSource::CreatePresentationDescriptor(
    IMFPresentationDescriptor** descriptor)
{
  CLog::LogF(LOGINFO, "MFDV: Packet source creating presentation descriptor");
  if (!descriptor)
    return E_POINTER;
  *descriptor = nullptr;
  std::lock_guard lock(m_mutex);
  if (m_shutdown)
    return MF_E_SHUTDOWN;
  if (!m_descriptorReady)
  {
    CLog::LogF(LOGERROR, "MFDV: Presentation descriptor requested before HEVC sequence header");
    return MF_E_INVALIDMEDIATYPE;
  }
  if (FAILED(m_descriptorStatus))
    return m_descriptorStatus;
  if (!m_streamDescriptor)
    return MF_E_NOT_INITIALIZED;
  IMFStreamDescriptor* streamDescriptors[] = {m_streamDescriptor.Get()};
  HRESULT hr = MFCreatePresentationDescriptor(1, streamDescriptors, descriptor);
  if (SUCCEEDED(hr))
    hr = (*descriptor)->SelectStream(0);
  if (FAILED(hr))
  {
    if (*descriptor)
    {
      (*descriptor)->Release();
      *descriptor = nullptr;
    }
    return hr;
  }
  return S_OK;
}

STDMETHODIMP CMFPacketSource::Start(IMFPresentationDescriptor* descriptor,
                                    const GUID* timeFormat,
                                    const PROPVARIANT* startPosition)
{
  CLog::LogF(LOGINFO, "MFDV: Packet source Start called");
  if (!descriptor || (timeFormat && *timeFormat != GUID_NULL))
    return E_INVALIDARG;
  if (!m_eventQueue || !m_streamDescriptor)
    return MF_E_SHUTDOWN;

  ComPtr<IMFStreamDescriptor> selectedDescriptor;
  BOOL selected = FALSE;
  HRESULT hr = descriptor->GetStreamDescriptorByIndex(0, &selected, &selectedDescriptor);
  if (FAILED(hr) || !selected)
    return FAILED(hr) ? hr : MF_E_INVALIDREQUEST;

  ComPtr<CMFPacketStream> stream = Make<CMFPacketStream>();
  if (!stream)
    return E_OUTOFMEMORY;
  hr = stream->Initialize(this, m_streamDescriptor.Get());
  if (FAILED(hr))
    return hr;

  {
    std::lock_guard lock(m_mutex);
    if (m_shutdown)
      return MF_E_SHUTDOWN;
    m_stream = stream;
    m_tokens.clear();
  }

  PROPVARIANT position;
  PropVariantInit(&position);
  if (startPosition)
    hr = PropVariantCopy(&position, startPosition);
  if (SUCCEEDED(hr))
    hr = m_eventQueue->QueueEventParamUnk(MENewStream, GUID_NULL, S_OK, stream.Get());
  if (SUCCEEDED(hr))
    hr = stream->QueueSimpleEvent(MEStreamStarted, &position);
  if (SUCCEEDED(hr))
    hr = m_eventQueue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &position);
  PropVariantClear(&position);
  return hr;
}

STDMETHODIMP CMFPacketSource::Stop()
{
  ComPtr<IMFMediaStream> stream;
  {
    std::lock_guard lock(m_mutex);
    if (m_shutdown)
      return MF_E_SHUTDOWN;
    stream = std::move(m_stream);
    m_tokens.clear();
  }
  if (stream)
    stream->QueueEvent(MEStreamStopped, GUID_NULL, S_OK, nullptr);
  return m_eventQueue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

STDMETHODIMP CMFPacketSource::Pause()
{
  ComPtr<IMFMediaStream> stream;
  {
    std::lock_guard lock(m_mutex);
    if (m_shutdown)
      return MF_E_SHUTDOWN;
    stream = m_stream;
  }
  if (stream)
    stream->QueueEvent(MEStreamPaused, GUID_NULL, S_OK, nullptr);
  return m_eventQueue->QueueEventParamVar(MESourcePaused, GUID_NULL, S_OK, nullptr);
}

STDMETHODIMP CMFPacketSource::Shutdown()
{
  ComPtr<IMFMediaStream> stream;
  {
    std::lock_guard lock(m_mutex);
    if (m_shutdown)
      return S_OK;
    m_shutdown = true;
    m_samples.clear();
    m_tokens.clear();
    stream = std::move(m_stream);
  }
  if (stream)
    static_cast<CMFPacketStream*>(stream.Get())->Shutdown();
  if (m_eventQueue)
  {
    m_eventQueue->Shutdown();
    m_eventQueue.Reset();
  }
  m_streamDescriptor.Reset();
  return S_OK;
}