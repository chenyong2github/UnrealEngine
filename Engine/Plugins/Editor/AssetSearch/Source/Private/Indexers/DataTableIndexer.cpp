// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataTableIndexer.h"
#include "Utility/IndexerUtilities.h"
#include "Engine/DataTable.h"

enum class EDataTableIndexerVersion
{
	Empty = 0,
	Initial = 1,

	Current = Initial,
};

int32 FDataTableIndexer::GetVersion() const
{
	return (int32)EDataTableIndexerVersion::Current;
}

void FDataTableIndexer::IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer)
{
	const UDataTable* DataTable = Cast<UDataTable>(InAssetObject);
	check(DataTable);

	const TMap<FName, uint8*>& Rows = DataTable->GetRowMap();

	Serializer.BeginIndexingObject(DataTable, TEXT("$self"));
	for (const auto& Entry : Rows)
	{
		const FName& RowName = Entry.Key;
		uint8* Row = Entry.Value;

		FIndexerUtilities::IterateIndexableProperties(DataTable->GetRowStruct(), Row, [&Serializer](const FProperty* Property, const FString& Value) {
			Serializer.IndexProperty(Property, Value);
		});
	}
	Serializer.EndIndexingObject();
}