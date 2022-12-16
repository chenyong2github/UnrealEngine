// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "IAssetTypeActions.h"
#include "AssetDefinition_PoseAsset.h"

#define LOCTEXT_NAMESPACE "FEngineAssetDefinitionsModule"

class FEngineAssetDefinitionsModule : public IModuleInterface
{
public:

	// IModuleInterface interface
	virtual void StartupModule() override
	{
		// Register asset types... for now
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_PoseAsset));
	}

	virtual void ShutdownModule() override
	{
		// Unregister all the asset types that we registered
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
			for (int32 Index = 0; Index < CreatedAssetTypeActions.Num(); ++Index)
			{
				AssetTools.UnregisterAssetTypeActions(CreatedAssetTypeActions[Index].ToSharedRef());
			}
		}
		CreatedAssetTypeActions.Empty();
	}

private:
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	}

	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEngineAssetDefinitionsModule, EngineAssetDefinitions);
