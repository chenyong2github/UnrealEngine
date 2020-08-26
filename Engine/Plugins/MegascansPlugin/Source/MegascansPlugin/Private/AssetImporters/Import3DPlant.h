// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "AssetImporters/ImportSurface.h"
#include "Utilities/AssetData.h"


class UMaterialInstanceConstant;

enum PlantImportType {
	BILLBOARD_ONLY,
	NORMAL_ONLY,
	BLLBOARD_NORMAL
};

enum PlantPrefsType
{
	NORMALPrefs,
	BILLBOARDPrefs
};

class FImportPlant : public  IImportAsset
{
private:
	FImportPlant() = default;
	static TSharedPtr<FImportPlant> ImportPlantInst;	
	PlantImportType GetImportType(TSharedPtr<FAssetTypeData> AssetImportData);
	
	void PopulateBillboardTextures(TSharedPtr<FAssetTypeData> AssetImportData);
	void SetBillboardImportParams(TSharedPtr<SurfaceImportParams> SImportParams);
	TMap<FString, FString> ImportPlants(TSharedPtr<FAssetTypeData> AssetImportData, TSharedPtr<ImportParams3DPlantAsset> AssetPlantParameters);
	void ApplyMaterial(PlantImportType ImportType, UMaterialInstanceConstant* MaterialInstance, UMaterialInstanceConstant* BillboardInstance, TMap<FString, FString> ImportedPlants, bool bSavePackage = false);
	void SetLodScreenSizes(TSharedPtr<FAssetTypeData> AssetImportData, TMap<FString, FString> ImportedPlants);
	void SetLodScreenSizes(UStaticMesh* SourceMesh, TMap<FString, float>& LodScreenSizes, const TArray<FString>& SelectedLods);
public:
	static TSharedPtr<FImportPlant> Get();
	virtual void ImportAsset(TSharedPtr<FAssetTypeData> AssetImportData) override;
	
};