// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementAssetDataInterface.h"

TArray<FAssetData> UTypedElementAssetDataInterface::GetAllReferencedAssetDatas(const FTypedElementHandle& InElementHandle)
{
	TArray<FAssetData> AssetDatas;

	FAssetData ElementAssetData = GetAssetData(InElementHandle);
	if (ElementAssetData.IsValid())
	{
		AssetDatas.Emplace(ElementAssetData);
	}

	return AssetDatas;
}

FAssetData UTypedElementAssetDataInterface::GetAssetData(const FTypedElementHandle& InElementHandle)
{
	return FAssetData();
}
