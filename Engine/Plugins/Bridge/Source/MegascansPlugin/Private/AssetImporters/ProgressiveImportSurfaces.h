// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MSAssetImportData.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceConstant.h"

struct FProgressiveSurfaces
{	
	AStaticMeshActor* ActorInLevel;
	UMaterialInstanceConstant* PreviewInstance;
	FString PreviewFolderPath;
	FString PreviewMeshPath;

};

class FImportProgressiveSurfaces
{

private:
	FImportProgressiveSurfaces() = default;
	static TSharedPtr<FImportProgressiveSurfaces> ImportProgressiveSurfacesInst;

	TMap<FString, TSharedPtr<FProgressiveSurfaces>> PreviewDetails;
	void SpawnMaterialPreviewActor(FString AssetID);	


public:
	static TSharedPtr<FImportProgressiveSurfaces> Get();
	void ImportAsset(TSharedPtr<FJsonObject> AssetImportJson);

	void HandlePreviewInstanceLoad(FAssetData PreviewInstanceData, FString AssetID);
	void HandlePreviewTextureLoad(FAssetData TextureData, FString AssetID, FString Type);

	void HandleHighInstanceLoad(FAssetData HighInstanceData, FString AssetID, FUAssetMeta AssetMetaData);
	
};