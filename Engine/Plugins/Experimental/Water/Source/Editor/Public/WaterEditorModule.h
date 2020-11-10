// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Engine/EngineTypes.h"
#include "AssetTypeCategories.h"
#include "IAssetTypeActions.h"

class FComponentVisualizer;

WATEREDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogWaterEditor, Log, All);

class IWaterEditorModuleInterface : public IModuleInterface
{
public:

};

class FWaterEditorModule : public IWaterEditorModuleInterface
{
public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	static EAssetTypeCategories::Type GetAssetCategory() { return WaterAssetCategory; }

private:
	void RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer);

	void OnLevelActorAddedToWorld(AActor* Actor);

	void OnMapLoaded(const FString& /* Filename */, bool /*bAsTemplate*/);

private:
	/** Array of component class names we have registered, so we know what to unregister afterwards */
	TArray<FName> RegisteredComponentClassNames;

	static EAssetTypeCategories::Type WaterAssetCategory;

	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions;
};