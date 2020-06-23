// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataAssetIndexer.h"
#include "Utility/IndexerUtilities.h"
#include "Engine/DataAsset.h"
#include "SearchSerializer.h"

enum class EDataAssetIndexerVersion
{
	Empty,
	Initial,

	// -----<new versions can be added above this line>-------------------------------------------------
	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

int32 FDataAssetIndexer::GetVersion() const
{
	return (int32)EDataAssetIndexerVersion::LatestVersion;
}

void FDataAssetIndexer::IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer) const
{
	Serializer.BeginIndexingObject(InAssetObject, TEXT("$self"));

	FIndexerUtilities::IterateIndexableProperties(InAssetObject, [&Serializer](const FProperty* Property, const FString& Value) {
		Serializer.IndexProperty(Property, Value);
	});

	Serializer.EndIndexingObject();
}