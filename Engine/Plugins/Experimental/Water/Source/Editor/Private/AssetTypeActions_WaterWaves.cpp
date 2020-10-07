// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_WaterWaves.h"
#include "WaterWaves.h"
#include "WaterEditorModule.h"

#define LOCTEXT_NAMESPACE "WaterWaves"

FText FAssetTypeActions_WaterWaves::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "FAssetTypeActions_WaterWaves", "Water Waves");
}

UClass* FAssetTypeActions_WaterWaves::GetSupportedClass() const 
{
	return UWaterWavesAsset::StaticClass();
}

uint32 FAssetTypeActions_WaterWaves::GetCategories() 
{
	return FWaterEditorModule::GetAssetCategory();
}

#undef LOCTEXT_NAMESPACE