#pragma once
#include "CoreMinimal.h"
#include "AssetPreferencesData.h"


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

class FImportPlant
{
private:
	FImportPlant() = default;
	static TSharedPtr<FImportPlant> ImportPlantInst;	
	PlantImportType GetImportType(TSharedPtr<FAssetTypeData> AssetImportData);
	TSharedPtr<FSurfacePreferences> GetSurfacePrefs(TSharedPtr<F3dPlantPreferences> Type3dPlantPrefs);
	void PopulateBillboardTextures(TSharedPtr<FAssetTypeData> AssetImportData);
	void SetBillboardImportParams(TSharedPtr<SurfaceImportParams> SImportParams);
	TArray<FString> ImportPlants(TSharedPtr<FAssetTypeData> AssetImportData, TSharedPtr<SurfaceImportParams> SImportParams);
	void ApplyMaterial(PlantImportType ImportType, UMaterialInstanceConstant* MaterialInstance, UMaterialInstanceConstant* BillboardInstance, TArray<FString> ImportedPlants, int32 BillboardLodIndex = 0);


public:

	static TSharedPtr<FImportPlant> Get();
	void ImportPlant(TSharedPtr<F3dPlantPreferences> Type3dPrefs, TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance=nullptr);

};