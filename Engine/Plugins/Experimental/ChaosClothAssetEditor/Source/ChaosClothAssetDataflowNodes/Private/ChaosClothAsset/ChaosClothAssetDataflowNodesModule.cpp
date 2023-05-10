// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ChaosClothAssetDataflowNodesModule.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "ChaosClothAsset/WeightedValueCustomization.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FChaosClothAssetDataflowNodesModule"

void FChaosClothAssetDataflowNodesModule::StartupModule()
{
	using namespace UE::Chaos::ClothAsset;

	DataflowNodes::Register();

	// Register type customizations
	if (FPropertyEditorModule* const PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->RegisterCustomPropertyTypeLayout("ChaosClothAssetWeightedValue", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FWeightedValueCustomization::MakeInstance));
	}
}

void FChaosClothAssetDataflowNodesModule::ShutdownModule()
{
	// Unregister type customizations
	if (FPropertyEditorModule* const PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout("ChaosClothAssetWeightedValue");
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FChaosClothAssetDataflowNodesModule, ChaosClothAssetDataflowNodes)
