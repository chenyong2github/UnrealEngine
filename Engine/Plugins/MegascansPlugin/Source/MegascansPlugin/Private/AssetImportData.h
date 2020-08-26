// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(MSLiveLinkLog, Log, All);

struct FAssetMetaData {
	FString Resolution;
	FString Category;
	FString Type;
	FString Id;
	FString Name;
	FString Path;
	FString TextureFormat;
	TArray<FString> Tags;
	FString ActiveLOD;
	TArray<FString> Categories;
	FString ExportPath;
	FString NamingConvention;
	FString FolderNamingConvention;
	FString MinLOD;
	bool bUseBillboardMaterial;
	TMap<FString, TArray<int8>> MaterialTypes;
	bool bIsModularWindow;
	bool bSavePackages;
};



struct FAssetTextureData {
	FString Format;
	FString Type;
	FString Resolution;
	FString Name;
	FString NameOverride;
	FString Path;
};

struct FAssetBillboardData
{
	FString Type;	
	FString Path;
};

struct FAssetPackedTextures {
	TSharedPtr<FAssetTextureData> PackedTextureData;
	TMap<FString, TArray<FString>> ChannelData;
};

struct FAssetMeshData {
	FString Format;
	FString Type;
	FString Resolution;
	FString Name;
	FString NameOverride;
	FString Path;
};



struct FAssetLodData {
	FString Lod;
	FString Path;
	FString Name;
	FString NameOverride;
	FString LodObjectName;
	FString Format;
	FString Type;
};



struct FAssetTypeData {
	TSharedPtr<FAssetMetaData> AssetMetaInfo;
	TArray<TSharedPtr<FAssetTextureData>> TextureComponents;
	TArray<TSharedPtr<FAssetMeshData>> MeshList;
	TArray<TSharedPtr<FAssetLodData>> LodList;
	TArray<TSharedPtr<FAssetPackedTextures>> PackedTextures;

	TArray<TSharedPtr<FAssetBillboardData>> BillboardTextures;
	TMap<FString, TMap<FString, float>> PlantsLodScreenSizes;
};


struct FAssetsData {
	TArray<TSharedPtr<FAssetTypeData>> AllAssetsData;
};

struct FDHIData {
	FString CharacterPath;
	FString CommonPath;
	FString RootPath;
	FString CharacterName;
};
