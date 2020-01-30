// Copyright Epic Games, Inc. All Rights Reserved.

#include "VivoxVoiceChatModule.h"

#include "Modules/ModuleManager.h"

#include "vivoxclientapi/util.h"

IMPLEMENT_MODULE(FVivoxVoiceChatModule, VivoxVoiceChat);

extern TUniquePtr<FVivoxVoiceChat> CreateVivoxObject();

static void* VivoxClientApiAllocate(size_t bytes)
{
	LLM_SCOPE(ELLMTag::AudioVoiceChat);
	return FMemory::Malloc(bytes);
}

static void VivoxClientApiDeallocate(void* ptr)
{
	LLM_SCOPE(ELLMTag::AudioVoiceChat);
	FMemory::Free(ptr);
}

void FVivoxVoiceChatModule::StartupModule()
{
	// VivoxClientApi allocator hooks need to be setup here as they are used in CreateVivoxObject
	VivoxClientApi::SetMemFunctions(&VivoxClientApiAllocate, &VivoxClientApiDeallocate);

	VivoxObj = CreateVivoxObject();
	if (VivoxObj.IsValid())
	{
		IModularFeatures::Get().RegisterModularFeature(TEXT("VoiceChat"), VivoxObj.Get());
	}
}

void FVivoxVoiceChatModule::ShutdownModule()
{
	if (VivoxObj.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(TEXT("VoiceChat"), VivoxObj.Get());
		VivoxObj->Uninitialize();
		VivoxObj.Reset();
	}
}
