// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

class IOnlineSubsystemUtils;

class FLSALiveLinkModule : public IModuleInterface
{
public:

	FLSALiveLinkModule() = default;
	virtual ~FLSALiveLinkModule() = default;

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
		static FName ModuleName = FName(TEXT("LSALiveLink"));
		return ModuleName;
	}
};