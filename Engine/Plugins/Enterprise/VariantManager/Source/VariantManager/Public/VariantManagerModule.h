// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"		// For inline LoadModuleChecked()

#define VARIANTMANAGERMODULE_MODULE_NAME TEXT("VariantManager")

class AActor;
class FVariantManager;
class ULevelVariantSets;

class IVariantManagerModule : public IModuleInterface
{
public:
	static inline IVariantManagerModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IVariantManagerModule>(VARIANTMANAGERMODULE_MODULE_NAME);
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(VARIANTMANAGERMODULE_MODULE_NAME);
	}

	virtual TSharedRef<FVariantManager> CreateVariantManager(ULevelVariantSets* InLevelVariantSets) = 0;
	virtual UObject* CreateLevelVariantSetsAssetWithDialog() = 0;
	virtual UObject* CreateLevelVariantSetsAsset(const FString& AssetName, const FString& PackagePath, bool bForceOverwrite = false) = 0;
	virtual AActor* GetOrCreateLevelVariantSetsActor(UObject* LevelVariantSetsAsset, bool bForceCreate=false) = 0;
};

