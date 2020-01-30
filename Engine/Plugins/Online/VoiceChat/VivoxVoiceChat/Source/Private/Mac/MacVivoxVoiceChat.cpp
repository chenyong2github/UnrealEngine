// Copyright Epic Games, Inc. All Rights Reserved.

#include "MacVivoxVoiceChat.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

TUniquePtr<FVivoxVoiceChat> CreateVivoxObject()
{
	return MakeUnique<FMacVivoxVoiceChat>();
}

FMacVivoxVoiceChat::FMacVivoxVoiceChat()
	: VivoxORTPHandle(nullptr)
	, VivoxSDKHandle(nullptr)
{
}

FMacVivoxVoiceChat::~FMacVivoxVoiceChat()
{
	UnloadVivoxModules();
}

bool FMacVivoxVoiceChat::Initialize()
{
	bool bReturn = false;
	if (LoadVivoxModules())
	{
		bReturn = FVivoxVoiceChat::Initialize();
	}

	return bReturn;
}

bool FMacVivoxVoiceChat::LoadVivoxModules()
{
	const FString VivoxORTPFile = TEXT("libortp.dylib");
	const FString VivoxSDKFile = TEXT("libvivoxsdk.dylib");

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

void FMacVivoxVoiceChat::UnloadVivoxModules()
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
