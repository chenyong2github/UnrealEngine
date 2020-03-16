// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintIndexer.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Components/Widget.h"
#include "Utility/IndexerUtilities.h"

#define LOCTEXT_NAMESPACE "FWidgetBlueprintIndexer"

enum class EWidgetBlueprintIndexerVersion
{
	Empty = 0,
	Initial = 1,

	// -----<new versions can be added above this line>-------------------------------------------------
	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

int32 FWidgetBlueprintIndexer::GetVersion() const
{
	return (int32)EWidgetBlueprintIndexerVersion::LatestVersion;
}

void FWidgetBlueprintIndexer::IndexAsset(const UObject* InAssetObject, FSearchSerializer& Serializer) const
{
	const UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(InAssetObject);
	check(BP);

	TArray<const UWidget*> AllWidgets = BP->GetAllSourceWidgets();

	for (const UWidget* Widget : AllWidgets)
	{
		Serializer.BeginIndexingObject(Widget, Widget->GetDisplayLabel());
		Serializer.IndexProperty(TEXT("Name"), Widget->GetDisplayLabel());

		FIndexerUtilities::IterateIndexableProperties(Widget, [&Serializer](const FProperty* Property, const FString& Value) {
			Serializer.IndexProperty(Property, Value);
		});

		Serializer.EndIndexingObject();
	}
}

#undef LOCTEXT_NAMESPACE