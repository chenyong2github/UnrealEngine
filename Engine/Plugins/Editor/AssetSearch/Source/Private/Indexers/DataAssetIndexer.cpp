// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataAssetIndexer.h"
#include "Utility/IndexerUtilities.h"
#include "Engine/DataAsset.h"

enum class EDataAssetIndexerVersion
{
	Empty = 0,
	Initial = 1,

	Current = Initial,
};

int32 FDataAssetIndexer::GetVersion() const
{
	return (int32)EDataAssetIndexerVersion::Current;
}

void FDataAssetIndexer::IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer)
{
	Serializer.BeginIndexingObject(InAssetObject, TEXT("$self"));

	FIndexerUtilities::IterateIndexableProperties(InAssetObject, [&Serializer](const FProperty* Property, const FString& Value) {
		Serializer.IndexProperty(Property, Value);
	});

	Serializer.EndIndexingObject();
}