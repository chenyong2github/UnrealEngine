#pragma once
#include "CoreMinimal.h"
#include "AssetImportData.h"
#include "Utilities/MiscUtils.h"

class FAssetDataHandler {
private:
	FAssetDataHandler();
	TSharedPtr<FAssetsData> AssetsImportData ;
	

	FString GetAssetName(const FString & AssetName);
	FString ConcatJsonString(const FString& AssetsImportJson);
	static TSharedPtr<FAssetDataHandler> AssetDataHandlerInst;

	TSharedPtr<FAssetTypeData> GetAssetData(TSharedPtr<FJsonObject> AssetDataObject);

	TSharedPtr< FAssetPackedTextures> GetPackedTextureData(TSharedPtr<FJsonObject> PackedDataObject);
	TSharedPtr<FAssetMetaData> GetAssetMetaData(TSharedPtr<FJsonObject> MetaDataObject);
	TSharedPtr<FAssetTextureData> GetAssetTextureData(TSharedPtr<FJsonObject> TextureDataObject);
	TSharedPtr<FAssetMeshData> GetAssetMeshData(TSharedPtr<FJsonObject> MeshDataObject);
	TSharedPtr<FAssetLodData> GetAssetLodData(TSharedPtr<FJsonObject> LodDataObject);

	TSharedPtr<FAssetBillboardData> GetBillboardData(TSharedPtr<FJsonObject> BillboardObject);
	
	

public:	
	static TSharedPtr<FAssetDataHandler> Get();
	TSharedPtr<FAssetsData> GetAssetsData(const FString& AssetsImportJson);
};