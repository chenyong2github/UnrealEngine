// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkLegacyAssetTypeActions.h"
#include "NeuralNetworkLegacy.h"
#include "NeuralNetworkInferenceEditorModule.h"
#include "EditorFramework/AssetImportData.h"


FText FNeuralNetworkLegacyAssetTypeActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_NeuralNetworkLegacy", "Neural Network Legacy (Deprecated)");
}

FColor FNeuralNetworkLegacyAssetTypeActions::GetTypeColor() const
{
	return FColor::Red;
}

UClass* FNeuralNetworkLegacyAssetTypeActions::GetSupportedClass() const
{
	return UNeuralNetworkLegacy::StaticClass();
}

bool FNeuralNetworkLegacyAssetTypeActions::HasActions(const TArray<UObject*>& InObjects) const
{
	// Returns true if this class can supply actions for InObjects
	return false;
}

uint32 FNeuralNetworkLegacyAssetTypeActions::GetCategories()
{
	const INeuralNetworkInferenceEditorModule& NeuralNetworkInferenceEditorModule = FModuleManager::GetModuleChecked<INeuralNetworkInferenceEditorModule>("NeuralNetworkInferenceEditor");
	return NeuralNetworkInferenceEditorModule.GetMLAssetCategoryBit(); // Or EAssetTypeCategories::Misc
}

bool FNeuralNetworkLegacyAssetTypeActions::IsImportedAsset() const
{
	// Returns whether the asset was imported from an external source
	return true;
}

void FNeuralNetworkLegacyAssetTypeActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	// Collects the resolved source paths for the imported assets
	for (UObject* Asset : TypeAssets)
	{
		const UNeuralNetworkLegacy* const Network = CastChecked<UNeuralNetworkLegacy>(Asset);
		if (Network && Network->GetAssetImportData())
		{
			Network->GetAssetImportData()->ExtractFilenames(OutSourceFilePaths);
		}
		else
		{
			OutSourceFilePaths.Add(TEXT(""));
		}
	}
}
