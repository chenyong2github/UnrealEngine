#pragma once
#include "CoreMinimal.h"

class UMaterialInstanceConstant;
struct FAssetTypeData;
struct FSurfacePreferences;

class FMaterialUtils
{
public:
	static UMaterialInstanceConstant* CreateInstanceMaterial(const FString& MasterMaterialPath, const FString& InstanceDestination, const FString& AssetName);
	static FString GetMasterMaterial(TSharedPtr<FSurfacePreferences> TypeSurfacePrefs, TSharedPtr<FAssetTypeData> AssetImportData, const FString& MasterMaterialOverride = TEXT(""));

};