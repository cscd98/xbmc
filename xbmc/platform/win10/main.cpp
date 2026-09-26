/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "pch.h"

#include "platform/win10/Win10App.h"

#include <windows.h>

// TODO: this is a temp hack due rust bringing in std symbols
// which lead to a linking failure

// Undefine macro if windows.h defined QueryDosDeviceW -> QueryDosDeviceW
#ifdef QueryDosDeviceW
#pragma push_macro("QueryDosDeviceW")
#undef QueryDosDeviceW
#endif

extern "C" {

// Stub for QueryDosDeviceW (unresolved by std::sys::pal::windows)
DWORD __stdcall QueryDosDeviceW(LPCWSTR lpDeviceName, LPWSTR lpTargetPath, DWORD ucchMax) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

// Stub for NtSetInformationFile (unresolved by std::sys::fs::windows)
long __stdcall NtSetInformationFile(
    HANDLE FileHandle,
    void* IoStatusBlock,
    void* FileInformation,
    unsigned long Length,
    unsigned long FileInformationClass)
{
    return (long)0xC0000002L; // STATUS_NOT_IMPLEMENTED
}

// Stub for NtCreateNamedPipeFile (unresolved by std::sys::process::windows)
long __stdcall NtCreateNamedPipeFile(
    PHANDLE FileHandle,
    unsigned long DesiredAccess,
    void* ObjectAttributes,
    void* IoStatusBlock,
    unsigned long ShareAccess,
    unsigned long CreateDisposition,
    unsigned long CreateOptions,
    unsigned long NamedPipeType,
    unsigned long ReadMode,
    unsigned long CompletionMode,
    unsigned long MaximumInstances,
    unsigned long InboundQuota,
    unsigned long OutboundQuota,
    void* DefaultTimeout)
{
    return (long)0xC0000002L; // STATUS_NOT_IMPLEMENTED
}

}

#pragma pop_macro("QueryDosDeviceW")

using namespace KODI::PLATFORM::WINDOWS10;

int __stdcall WinMain(HINSTANCE, HINSTANCE, PCSTR, int)
{
  winrt::init_apartment();
  winrt::Windows::ApplicationModel::Core::CoreApplication::Run(winrt::make<App>());
}
