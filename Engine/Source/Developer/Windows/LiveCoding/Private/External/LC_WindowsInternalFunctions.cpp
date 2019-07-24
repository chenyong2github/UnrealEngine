// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#include "LC_WindowsInternalFunctions.h"


windowsInternal::Function<decltype(NtSuspendProcess)> windowsInternal::NtSuspendProcess("ntdll.dll", "NtSuspendProcess");
windowsInternal::Function<decltype(NtResumeProcess)> windowsInternal::NtResumeProcess("ntdll.dll", "NtResumeProcess");
windowsInternal::Function<decltype(NtWriteVirtualMemory)> windowsInternal::NtWriteVirtualMemory("ntdll.dll", "NtWriteVirtualMemory");
windowsInternal::Function<decltype(NtQuerySystemInformation)> windowsInternal::NtQuerySystemInformation("ntdll.dll", "NtQuerySystemInformation");
windowsInternal::Function<decltype(NtQueryInformationProcess)> windowsInternal::NtQueryInformationProcess("ntdll.dll", "NtQueryInformationProcess");
windowsInternal::Function<decltype(NtContinue)> windowsInternal::NtContinue("ntdll.dll", "NtContinue");
