// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceQAAssetTypeActions.h"
#include "NeuralNetworkInferenceEditorModule.h"
#include "NeuralNetworkInferenceQAAsset.h"


FText FNeuralNetworkInferenceQAAssetTypeActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_NeuralNetworkInferenceQAAsset", "Neural Network Inference QA Asset");
}

FColor FNeuralNetworkInferenceQAAssetTypeActions::GetTypeColor() const
{
	return FColor::Red;
}

UClass* FNeuralNetworkInferenceQAAssetTypeActions::GetSupportedClass() const
{
	return UNeuralNetworkInferenceQAAsset::StaticClass();
}

bool FNeuralNetworkInferenceQAAssetTypeActions::HasActions(const TArray<UObject*>& InObjects) const
{
	// Returns true if this class can supply actions for InObjects
	return false;
}

uint32 FNeuralNetworkInferenceQAAssetTypeActions::GetCategories()
{
	const INeuralNetworkInferenceEditorModule& NeuralNetworkInferenceEditorModule = FModuleManager::GetModuleChecked<INeuralNetworkInferenceEditorModule>("NeuralNetworkInferenceEditor");
	return NeuralNetworkInferenceEditorModule.GetMLAssetCategoryBit(); // Or EAssetTypeCategories::Misc
}

bool FNeuralNetworkInferenceQAAssetTypeActions::IsImportedAsset() const
{
	// Returns whether the asset was imported from an external source
	return true;
}
