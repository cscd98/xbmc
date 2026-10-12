/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <mfapi.h>
#include <mftransform.h>
#include <winrt/Windows.ApplicationModel.AppService.h>
#include <wrl/client.h>

extern "C"
{
#include <libavutil/pixfmt.h>
}

/*!
 * \brief Runs decoded Dolby Vision frames through the system Dolby Vision video renderer
 * effect MFT, which reshapes P010 + RPU into display-referred RGB.
 */
class CDolbyVisionEffect
{
public:
  struct StreamInfo
  {
    int profile = 0;
    int compatibilityId = 0;
    AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
    AVColorTransferCharacteristic colorTransfer = AVCOL_TRC_UNSPECIFIED;
    bool fullRange = false;
    unsigned maxMasteringLuminance = 0; // nits, 0 if unknown
  };

  ~CDolbyVisionEffect();

  static bool IsAvailable();

  bool Open(bool hdrOutput, float fps, const StreamInfo& stream);
  bool IsHdrOutput() const { return m_hdrOutput; }
  DXGI_FORMAT GetOutputFormat() const;

  /*!
   * \return RGB texture owned by the effect, valid until the pool wraps around
   */
  ID3D11Texture2D* Process(ID3D11Resource* decoderTexture,
                           unsigned arrayIndex,
                           const std::vector<uint8_t>& rpu,
                           LONGLONG sampleTime);

private:
  void ConnectAppService();
  void SetOutputDevice();
  HRESULT ConfigureStream(unsigned width, unsigned height);
  HRESULT SetInputType(unsigned width, unsigned height);
  HRESULT SetOutputType();
  ID3D11Texture2D* GetInputTexture(const D3D11_TEXTURE2D_DESC& decoderDesc);
  ID3D11Texture2D* GetOutputTexture(unsigned width, unsigned height);

  Microsoft::WRL::ComPtr<IMFTransform> m_transform;
  Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_deviceManager;
  winrt::Windows::ApplicationModel::AppService::AppServiceConnection m_appService{nullptr};
  bool m_mfStarted{false};

  bool m_hdrOutput{false};
  float m_fps{0.0f};
  StreamInfo m_stream;

  bool m_hasDisplayColor{false};
  // red, green, blue, white xy; MT_CUSTOM_VIDEO_PRIMARIES layout
  std::array<float, 8> m_displayPrimaries{};
  float m_displayMaxLuminance{0.0f};
  float m_displayMinLuminance{0.0f};

  unsigned m_width{0};
  unsigned m_height{0};
  bool m_streaming{false};
  unsigned m_failures{0};

  std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 8> m_inputPool;
  size_t m_inputIndex{0};
  std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 16> m_outputPool;
  size_t m_outputIndex{0};
};
