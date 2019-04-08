// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DragAndDrop/CollectionDragDropOp.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"

TArray<FAssetData>  FCollectionDragDropOp::GetAssets() const
{
	ICollectionManager& CollectionManager = FModuleManager::LoadModuleChecked<FCollectionManagerModule>("CollectionManager").Get();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FName> AssetPaths;
	for (const FCollectionNameType& CollectionNameType : Collections)
	{
		TArray<FName> CollectionAssetPaths;
		CollectionManager.GetAssetsInCollection(CollectionNameType.Name, CollectionNameType.Type, CollectionAssetPaths);
		AssetPaths.Append(CollectionAssetPaths);
	}

	TArray<FAssetData> AssetDatas;
	AssetDatas.Reserve(AssetPaths.Num());
	for (const FName& AssetPath : AssetPaths)
	{
		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPath);
		if (AssetData.IsValid())
		{
			AssetDatas.AddUnique(AssetData);
		}
	}
	
	return AssetDatas;
}