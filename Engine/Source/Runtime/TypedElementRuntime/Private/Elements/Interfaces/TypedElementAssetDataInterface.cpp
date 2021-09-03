// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementAssetDataInterface.h"

TArray<FAssetData> ITypedElementAssetDataInterface::GetAllReferencedAssetDatas(const FTypedElementHandle& InElementHandle)
{
	TArray<FAssetData> AssetDatas;

	FAssetData ElementAssetData = GetAssetData(InElementHandle);
	if (ElementAssetData.IsValid())
	{
		AssetDatas.Emplace(ElementAssetData);
	}

	return AssetDatas;
}

FAssetData ITypedElementAssetDataInterface::GetAssetData(const FTypedElementHandle& InElementHandle)
{
	return FAssetData();
}
