// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Net/UnrealNetwork.h"
#include "Tickable.h"

class IOnlineSubsystemUtils;

class FForwardingChannelsModule : public IModuleInterface
{
public:

	FForwardingChannelsModule() = default;
	virtual ~FForwardingChannelsModule() = default;

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(GetModuleName());
	}

protected:

	static FName GetModuleName()
	{
		static FName ModuleName = FName(TEXT("ForwardingChannels"));
		return ModuleName;
	}
};