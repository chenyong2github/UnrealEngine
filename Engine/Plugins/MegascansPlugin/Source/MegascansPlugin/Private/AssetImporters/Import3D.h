// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "AssetImportData.h"
#include "ImportFactory.h"
#include "Utilities/AssetData.h"

class UMaterialInstanceConstant;

class FImport3d : public IImportAsset
{
private:
	FImport3d() = default;
	static TSharedPtr<FImport3d> Import3dInst;
	void ImportScatter(TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance, const FString& MeshDestination, const FString& AssetName);

	void ImportScatter(TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance, TSharedPtr<ImportParams3DAsset> Asset3DParameters);
	void ImportNormal(TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance, TSharedPtr<ImportParams3DAsset> Asset3DParameters);
public:
	static TSharedPtr<FImport3d> Get();
	
	virtual void ImportAsset(TSharedPtr<FAssetTypeData> AssetImportData) override;

};