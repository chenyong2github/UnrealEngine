// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WindowsVivoxVoiceChat.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

TUniquePtr<FVivoxVoiceChat> CreateVivoxObject()
{
	return MakeUnique<FWindowsVivoxVoiceChat>();
}

FWindowsVivoxVoiceChat::FWindowsVivoxVoiceChat()
	: VivoxORTPHandle(nullptr)
	, VivoxSDKHandle(nullptr)
{
}

FWindowsVivoxVoiceChat::~FWindowsVivoxVoiceChat()
{
	UnloadVivoxModules();
}

bool FWindowsVivoxVoiceChat::Initialize()
{
	bool bReturn = false;
	if (LoadVivoxModules())
	{
		bReturn = FVivoxVoiceChat::Initialize();
	}

	return bReturn;
}

bool FWindowsVivoxVoiceChat::LoadVivoxModules()
{
#if PLATFORM_64BITS
	const FString VivoxORTPFile = TEXT("ortp_x64.dll");
	const FString VivoxSDKFile = TEXT("vivoxsdk_x64.dll");
#elif PLATFORM_32BITS
	const FString VivoxORTPFile = TEXT("ortp.dll");
	const FString VivoxSDKFile = TEXT("vivoxsdk.dll");
#endif

	if (VivoxORTPHandle == nullptr)
	{
		VivoxORTPHandle = FPlatformProcess::GetDllHandle(*VivoxORTPFile);
	}

	if (VivoxSDKHandle == nullptr)
	{
		VivoxSDKHandle = FPlatformProcess::GetDllHandle(*VivoxSDKFile);
	}

	return VivoxORTPHandle && VivoxSDKHandle;
}

void FWindowsVivoxVoiceChat::UnloadVivoxModules()
{
	if (VivoxORTPHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(VivoxORTPHandle);
		VivoxORTPHandle = nullptr;
	}

	if (VivoxSDKHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(VivoxSDKHandle);
		VivoxSDKHandle = nullptr;
	}
}

