// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImportFactory.h"
#include "AssetImporters/ImportSurface.h"


TSharedPtr<F3DImportFactory> F3DImportFactory::Import3dInst;
TMap<FString, TSharedPtr<IImportAsset>> FAssetImportFactory::RegisteredImporters;
bool FAssetImportFactory::bImportersRegistered;

TSharedPtr<F3DImportFactory> F3DImportFactory::Get()
{
	if (!Import3dInst.IsValid())
	{
		Import3dInst = MakeShareable(new F3DImportFactory);
	}
	return Import3dInst;
}

bool FAssetImportFactory::RegisterImporters()
{
	//if (!bImportersRegistered) return true;

	RegisteredImporters[TEXT("3d")] = nullptr;
	//RegisteredImporters[TEXT("surface")] = FImportSurface::Get();
	return false;
}

void FAssetImportFactory::Import(TSharedPtr<FAssetTypeData> AssetImportData)
{
	RegisteredImporters[AssetImportData->AssetMetaInfo->Type]->ImportAsset(AssetImportData);
}

void F3DImportFactory::ImportAsset(TSharedPtr<FAssetTypeData> AssetImportData)
{

}
