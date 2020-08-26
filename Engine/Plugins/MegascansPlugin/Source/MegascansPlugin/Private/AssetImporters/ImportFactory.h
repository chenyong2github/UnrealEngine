// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "AssetImportData.h"



class IImportAsset
{
public:
	IImportAsset() = default;
	virtual ~IImportAsset() = default;
	virtual void ImportAsset(TSharedPtr<FAssetTypeData> AssetImportData) = 0;
};

class FAssetImportFactory
{
public:	
	FAssetImportFactory() = delete;
	static bool RegisterImporters();
	static void Import(TSharedPtr<FAssetTypeData> AssetImportData);
private :	
	static TMap<FString, TSharedPtr<IImportAsset>> RegisteredImporters;
	static bool bImportersRegistered ;

};


class F3DImportFactory : public IImportAsset
{
private:
	F3DImportFactory() = default;
	static TSharedPtr<F3DImportFactory> Import3dInst;
	virtual void ImportAsset(TSharedPtr<FAssetTypeData> AssetImportData);	

public:
	static TSharedPtr<F3DImportFactory> Get();
	

};









