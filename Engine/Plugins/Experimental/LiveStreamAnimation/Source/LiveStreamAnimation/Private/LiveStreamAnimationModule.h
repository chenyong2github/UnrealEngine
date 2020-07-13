// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Net/UnrealNetwork.h"
#include "Tickable.h"

class IOnlineSubsystemUtils;

class FLiveStreamAnimationModule : public IModuleInterface
{
public:

	FLiveStreamAnimationModule() = default;
	virtual ~FLiveStreamAnimationModule() = default;

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
		static FName ModuleName = FName(TEXT("LiveStreamAnimation"));
		return ModuleName;
	}
};