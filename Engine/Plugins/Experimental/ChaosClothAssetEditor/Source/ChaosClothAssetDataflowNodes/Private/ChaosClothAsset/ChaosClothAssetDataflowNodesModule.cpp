// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ChaosClothAssetDataflowNodesModule.h"
#include "ChaosClothAsset/DataflowNodes.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FChaosClothAssetDataflowNodesModule"

void FChaosClothAssetDataflowNodesModule::StartupModule()
{
	UE::Chaos::ClothAsset::DataflowNodes::Register();
}

void FChaosClothAssetDataflowNodesModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FChaosClothAssetDataflowNodesModule, ChaosClothAssetDataflowNodes)
