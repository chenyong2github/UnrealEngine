// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MSAssetImportData.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceConstant.h"

struct FProgressiveData
{
	AStaticMeshActor* ActorInLevel;
	UMaterialInstanceConstant* PreviewInstance;
	FString PreviewFolderPath;
	FString PreviewMeshPath;
	
	

};

class FImportProgressive3D
{

private:
	FImportProgressive3D() = default;
	static TSharedPtr<FImportProgressive3D> ImportProgressive3DInst;
	void SpawnAtCenter(FAssetData AssetData, TSharedPtr<FUAssetData> ImportData);
	TMap<FString, AStaticMeshActor*> ProgressiveData;
	
	TMap<FString, TSharedPtr<FProgressiveData>> PreviewDetails;
	

	void AsyncCacheData(FAssetData HighAssetData, FString AssetID, FUAssetMeta AssetMetaData, bool bWaitNaniteConversion=false);
	void SwitchHigh(FAssetData HighAssetData, FString AssetID);
	float LocationOffset = 0.0f;

public:
	static TSharedPtr<FImportProgressive3D> Get();
	void ImportAsset(TSharedPtr<FJsonObject> AssetImportJson);

	void HandlePreviewTextureLoad(FAssetData TextureData, FString AssetID, FString Type);
	void HandlePreviewInstanceLoad(FAssetData PreviewInstanceData, FString AssetID);
	void HandleHighAssetLoad(FAssetData HighAssetData, FString AssetID, FUAssetMeta AssetMetaData, bool bWaitNaniteConversion=false);
	
	void SetLocationOffset(float LocationOffset);

	~FImportProgressive3D() {UE_LOG(LogTemp, Error, TEXT("Destructor calledd..."))};

	
};