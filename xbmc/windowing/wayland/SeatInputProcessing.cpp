/*
 *  Copyright (C) 2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SeatInputProcessing.h"

#include "utils/log.h"

#include <cassert>
#include <memory>

using namespace KODI::WINDOWING::WAYLAND;

CSeatInputProcessing::CSeatInputProcessing(wayland::surface_t const& inputSurface, IInputHandler& handler)
: m_inputSurface{inputSurface}, m_handler{handler}
{
}

void CSeatInputProcessing::AddSeat(CSeat* seat)
{
  CLog::Log(LOGDEBUG, "SeatInputProcessing: AddSeat called for seat {} ({})", 
            seat->GetGlobalName(), seat->GetName());
  assert(m_seats.find(seat->GetGlobalName()) == m_seats.end());
  auto& seatState = m_seats.emplace(seat->GetGlobalName(), seat).first->second;

  CLog::Log(LOGDEBUG, "SeatInputProcessing: Creating keyboard processor");
  seatState.keyboardProcessor = std::make_unique<CInputProcessorKeyboard>(*this);
  CLog::Log(LOGDEBUG, "SeatInputProcessing: Adding keyboard handler to seat");
  seat->AddRawInputHandlerKeyboard(seatState.keyboardProcessor.get());
  
  CLog::Log(LOGDEBUG, "SeatInputProcessing: Creating pointer processor");
  seatState.pointerProcessor = std::make_unique<CInputProcessorPointer>(m_inputSurface, *this);
  CLog::Log(LOGDEBUG, "SeatInputProcessing: Adding pointer handler to seat");
  seat->AddRawInputHandlerPointer(seatState.pointerProcessor.get());
  
  CLog::Log(LOGDEBUG, "SeatInputProcessing: Creating touch processor");
  seatState.touchProcessor = std::make_unique<CInputProcessorTouch>(m_inputSurface);
  CLog::Log(LOGDEBUG, "SeatInputProcessing: Adding touch handler to seat");
  seat->AddRawInputHandlerTouch(seatState.touchProcessor.get());
  
  CLog::Log(LOGDEBUG, "SeatInputProcessing: AddSeat complete for seat {}", seat->GetGlobalName());
}

void CSeatInputProcessing::RemoveSeat(CSeat* seat)
{
  auto seatStateI = m_seats.find(seat->GetGlobalName());
  if (seatStateI != m_seats.end())
  {
    seat->RemoveRawInputHandlerKeyboard(seatStateI->second.keyboardProcessor.get());
    seat->RemoveRawInputHandlerPointer(seatStateI->second.pointerProcessor.get());
    seat->RemoveRawInputHandlerTouch(seatStateI->second.touchProcessor.get());
    m_seats.erase(seatStateI);
  }
}

void CSeatInputProcessing::OnPointerEnter(std::uint32_t seatGlobalName, std::uint32_t serial)
{
  m_handler.OnSetCursor(seatGlobalName, serial);
  m_handler.OnEnter(InputType::POINTER);
}

void CSeatInputProcessing::OnPointerLeave()
{
  m_handler.OnLeave(InputType::POINTER);
}

void CSeatInputProcessing::OnPointerEvent(XBMC_Event& event)
{
  m_handler.OnEvent(InputType::POINTER, event);
}

void CSeatInputProcessing::OnKeyboardEnter()
{
  m_handler.OnEnter(InputType::KEYBOARD);
}

void CSeatInputProcessing::OnKeyboardLeave()
{
  m_handler.OnLeave(InputType::KEYBOARD);
}

void CSeatInputProcessing::OnKeyboardEvent(XBMC_Event& event)
{
  m_handler.OnEvent(InputType::KEYBOARD, event);
}

void CSeatInputProcessing::SetCoordinateScale(std::int32_t scale)
{
  for (auto& seatPair : m_seats)
  {
    seatPair.second.touchProcessor->SetCoordinateScale(scale);
    seatPair.second.pointerProcessor->SetCoordinateScale(scale);
  }
}
