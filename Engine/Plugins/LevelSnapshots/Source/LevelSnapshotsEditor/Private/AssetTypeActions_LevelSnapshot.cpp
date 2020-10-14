// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_LevelSnapshot.h"

#include "LevelSnapshot.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_LevelSnapshot"

FText FAssetTypeActions_LevelSnapshot::GetName() const
{
	return LOCTEXT("AssetTypeActions_LevelSnapshot_Name", "Level Snapshot");
}

uint32 FAssetTypeActions_LevelSnapshot::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

UClass* FAssetTypeActions_LevelSnapshot::GetSupportedClass() const
{
	return ULevelSnapshot::StaticClass();
};

FColor FAssetTypeActions_LevelSnapshot::GetTypeColor() const
{
	return FColor(238, 181, 235, 255);
}

#undef LOCTEXT_NAMESPACE