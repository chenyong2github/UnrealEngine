// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphAssetTypeActions.h"
#include "PCGGraph.h"

FText FPCGGraphAssetTypeActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "PCGGraphAssetTypeActions", "PCG Graph");
}

UClass* FPCGGraphAssetTypeActions::GetSupportedClass() const
{
	return UPCGGraph::StaticClass();
}