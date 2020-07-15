// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "IAssetTypeActions.h"

class FLiveStreamAnimationEditorModule : public IModuleInterface
{
public:

	FLiveStreamAnimationEditorModule() = default;
	virtual ~FLiveStreamAnimationEditorModule() = default;

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
		static FName ModuleName = FName(TEXT("LiveStreamAnimationEditor"));
		return ModuleName;
	}

private:

	TSharedPtr<IAssetTypeActions> FrameTranslatorActions;
};