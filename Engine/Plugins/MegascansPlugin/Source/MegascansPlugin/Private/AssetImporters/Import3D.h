#pragma once
#include "CoreMinimal.h"
#include "AssetPreferencesData.h"

class UMaterialInstanceConstant;

class FImport3d
{
private:
	FImport3d() = default;
	static TSharedPtr<FImport3d> Import3dInst;
	void ImportScatter(TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance, const FString& MeshDestination, const FString& AssetName);

public:
	static TSharedPtr<FImport3d> Get();
	void Import3d(TSharedPtr<F3DPreferences> Type3dPrefs, TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance=nullptr);

};