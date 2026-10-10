/*
 *  Copyright (C) 2010-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <deque>
#include <mutex>
#include <vector>

#include <mfmediaengine.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

HRESULT FindDolbyVisionP5RendererEffect(IMFActivate** activation);

class CMFPacketStream;

class CMFPacketSource final
  : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                        IMFMediaSource>
{
public:
  HRESULT Initialize(unsigned width, unsigned height, unsigned fpsRate, unsigned fpsScale,
                     const uint8_t* sequenceHeader, size_t sequenceHeaderSize);
  HRESULT CreateEngineExtension(IMFMediaEngineExtension** extension);
  bool CanQueue();
  HRESULT SetSequenceHeader(const uint8_t* data, size_t size);
  HRESULT PushSample(IMFSample* sample);

  STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
  STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
  STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
  STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status,
                          const PROPVARIANT* value) override;
  STDMETHODIMP GetCharacteristics(DWORD* characteristics) override;
  STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
  STDMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                     const PROPVARIANT* startPosition) override;
  STDMETHODIMP Stop() override;
  STDMETHODIMP Pause() override;
  STDMETHODIMP Shutdown() override;

private:
  friend class CMFPacketStream;
  HRESULT RequestSample(IUnknown* token, CMFPacketStream* stream);
  HRESULT CreateStreamDescriptorLocked();

  Microsoft::WRL::ComPtr<IMFMediaEventQueue> m_eventQueue;
  Microsoft::WRL::ComPtr<IMFStreamDescriptor> m_streamDescriptor;
  Microsoft::WRL::ComPtr<IMFMediaStream> m_stream;
  std::deque<Microsoft::WRL::ComPtr<IMFSample>> m_samples;
  std::deque<Microsoft::WRL::ComPtr<IUnknown>> m_tokens;
  std::vector<uint8_t> m_sequenceHeader;
  std::mutex m_mutex;
  unsigned m_width{0};
  unsigned m_height{0};
  unsigned m_fpsRate{0};
  unsigned m_fpsScale{0};
  HRESULT m_descriptorStatus{S_OK};
  bool m_descriptorReady{false};
  bool m_shutdown{false};
};