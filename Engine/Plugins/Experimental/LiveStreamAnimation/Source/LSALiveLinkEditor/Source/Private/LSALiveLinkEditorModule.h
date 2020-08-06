// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "IAssetTypeActions.h"

class FLSALiveLinkEditorModule : public IModuleInterface
{
public:

	FLSALiveLinkEditorModule() = default;
	virtual ~FLSALiveLinkEditorModule() = default;

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
		static FName ModuleName = FName(TEXT("LSALiveLinkEditor"));
		return ModuleName;
	}

private:

	TSharedPtr<IAssetTypeActions> FrameTranslatorActions;
};