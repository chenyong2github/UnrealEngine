// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "AssetImportData.h"


enum EAtlasSubType {
	DECAL,
	ATLAS
};

enum ESurfaceSubType {
	METAL,
	IMPERFECTION,
	DISPLACEMENT,
	SURFACE
};

enum EAsset3DSubtype {
	MULTI_MESH,
	NORMAL_3D
};

enum ES3DMasterMaterialType {
	NORMAL,
	NORMAL_FUZZ,
	CHANNEL_PACK,
	NORMAL_DISPLACEMENT,
	MODULAR_WINDOWS,
	CUSTOM
};

enum E3DPlantBillboardMaterial
{
	NORMAL_BILLBOARD,
	CAMERA_FACING
};


struct SurfaceParams
{
	FString MaterialInstanceName;
	FString MasterMaterialName;
	FString MasterMaterialPath;
	FString MaterialInstanceDestination;
	FString TexturesDestination;
	ESurfaceSubType SSubType;
	bool bContainsPackedMaps;
};

struct Asset3DParams
{
	FString MeshDestination;
	ES3DMasterMaterialType Asset3DMaterialType;
	EAsset3DSubtype SubType;	
	TArray<TSharedPtr<SurfaceParams>> MaterialParams;
};

struct Asset3DPlantParams
{
	FString MeshDestination;
	FString FoliageDestination;
	E3DPlantBillboardMaterial BillboardMaterialType;
	TSharedPtr<SurfaceParams> BillboardMasterMaterial;
	TSharedPtr<SurfaceParams> PlantsMasterMaterial;
	
};

struct AssetImportParams
{	
	FString AssetName;	
	FString AssetDestination;	
};


// Main data structures
struct ImportParams3DAsset
{
	TSharedPtr<AssetImportParams> BaseParams;
	TSharedPtr<Asset3DParams> ParamsAssetType;
};

struct ImportParams3DPlantAsset
{
	TSharedPtr<AssetImportParams> BaseParams;
	TSharedPtr<Asset3DPlantParams> ParamsAssetType;
};

struct ImportParamsSurfaceAsset
{
	TSharedPtr<AssetImportParams> BaseParams;
	TSharedPtr<SurfaceParams> ParamsAssetType;
};





class FAssetImportParams {
private:
	FAssetImportParams() = default;

	const TMap<EAtlasSubType, FString> AtlasSubTypeMaterials = {
		{EAtlasSubType::ATLAS,"Atlas_Material"},
		{EAtlasSubType::DECAL,"Decal_MasterMaterial"}
	};

	const TMap<E3DPlantBillboardMaterial, FString> BillboardMaterials = {
		{E3DPlantBillboardMaterial::CAMERA_FACING,"Billboard_Material"},
		{E3DPlantBillboardMaterial::NORMAL_BILLBOARD,"Foliage_Material"}
	};
	//These values should be stored in a configuration file
	const TMap<ESurfaceSubType, FString> SurfaceSubTypeMaterials = { 
		{ESurfaceSubType::METAL,"MS_DefaultMaterial"},
		{ESurfaceSubType::IMPERFECTION,"Imperfection_MasterMaterial"} ,
		{ESurfaceSubType::DISPLACEMENT,"Displacement_MasterMaterial"}
	};

	const FString BrushMasterMaterial = TEXT("BrushDecal_Material");

	const TMap<ES3DMasterMaterialType, FString> MasterMaterialType = {
		{ES3DMasterMaterialType::CHANNEL_PACK , TEXT("MS_DefaultMaterial_CP")},
		{ES3DMasterMaterialType::MODULAR_WINDOWS , TEXT("")},
		{ES3DMasterMaterialType::NORMAL , TEXT("MS_DefaultMaterial")},
		{ES3DMasterMaterialType::NORMAL_DISPLACEMENT , TEXT("MS_DefaultMaterial_Displacement")},
		{ES3DMasterMaterialType::NORMAL_FUZZ , TEXT("MS_DefaultMaterial_Fuzzy")},
		{ES3DMasterMaterialType::CUSTOM , TEXT("Custom")}
	};

	const FString PlantsMasterMaterial = TEXT("Foliage_Material");
	
	static TSharedPtr<FAssetImportParams> ImportParamsInst;
	FString GetAssetTypePath(const FString& RootDestination, const FString& AssetType);
	EAtlasSubType GetAtlasSubtype(TSharedPtr<FAssetTypeData> AssetImportData);
	ESurfaceSubType GetSurfaceSubtype(TSharedPtr<FAssetTypeData> AssetImportData);
	EAsset3DSubtype Get3DSubtype(TSharedPtr<FAssetTypeData> AssetImportData);
	ES3DMasterMaterialType GetS3DMasterMaterialType(TSharedPtr<FAssetTypeData> AssetImportData);
	E3DPlantBillboardMaterial GetBillboardMaterialType(TSharedPtr<FAssetTypeData> AssetImportData);
	FString GetMasterMaterial(const FString& SelectedMaterial);

public:
	static TSharedPtr<FAssetImportParams> Get();	
//Asset related data
	TSharedPtr<ImportParams3DAsset> Get3DAssetsParams(TSharedPtr<FAssetTypeData> AssetImportData);
	TSharedPtr<ImportParamsSurfaceAsset> GetSurfaceParams(TSharedPtr<FAssetTypeData> AssetImportData);
	TSharedPtr<ImportParams3DPlantAsset> Get3DPlantParams(TSharedPtr<FAssetTypeData> AssetImportData);
	TSharedPtr<AssetImportParams> GetAssetImportParams(TSharedPtr<FAssetTypeData> AssetImportData);
	TSharedPtr<ImportParamsSurfaceAsset> GetAtlasBrushParams(TSharedPtr<FAssetTypeData> AssetImportData);
};











