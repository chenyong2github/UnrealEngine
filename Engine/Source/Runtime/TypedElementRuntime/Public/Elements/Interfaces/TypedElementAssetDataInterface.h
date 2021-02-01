// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"
#include "AssetRegistry/AssetData.h"

#include "TypedElementAssetDataInterface.generated.h"

UCLASS(Abstract)
class TYPEDELEMENTRUNTIME_API UTypedElementAssetDataInterface : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	/**
	 * Returns any asset datas for content objects referenced by handle.
	 * If the given handle itself has valid asset data, it should be returned as the last element of the array.
	 *
	 * @returns An array of valid asset datas.
	 */
	UFUNCTION(BlueprintPure, Category = "TypedElementInterfaces|AssetData")
	virtual TArray<FAssetData> GetAllReferencedAssetDatas(const FTypedElementHandle& InElementHandle);

	/**
	 * Returns the asset data for the given handle, if it exists.
	 */
	UFUNCTION(BlueprintPure, Category = "TypedElementInterfaces|AssetData")
	virtual FAssetData GetAssetData(const FTypedElementHandle& InElementHandle);
};

template <>
struct TTypedElement<UTypedElementAssetDataInterface> : public TTypedElementBase<UTypedElementAssetDataInterface>
{
	TArray<FAssetData> GetAllReferencedAssetDatas() const { return InterfacePtr->GetAllReferencedAssetDatas(*this); }
	FAssetData GetAssetData() const { return InterfacePtr->GetAssetData(*this); }
};
