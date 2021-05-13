// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusTestGraphAssetActions.h"

FOptimusTestGraphAssetActions::FOptimusTestGraphAssetActions(EAssetTypeCategories::Type InAssetCategoryBit)
	: AssetCategoryBit(InAssetCategoryBit)
{
}

FText FOptimusTestGraphAssetActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "OptimusTestGraphActions", "Optimus Test Graph");
}

FColor FOptimusTestGraphAssetActions::GetTypeColor() const
{
	return FColor(200, 0, 0);
}

UClass* FOptimusTestGraphAssetActions::GetSupportedClass() const
{
	return UOptimusTestGraph::StaticClass();
}

bool FOptimusTestGraphAssetActions::CanFilter()
{
	return true;
}

bool FOptimusTestGraphAssetActions::CanLocalize() const
{
	return false;
}

uint32 FOptimusTestGraphAssetActions::GetCategories()
{
	return AssetCategoryBit;
}
