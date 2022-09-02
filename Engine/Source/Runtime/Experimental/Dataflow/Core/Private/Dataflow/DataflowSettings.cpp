// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSettings.h"
#include "Dataflow/DataflowNodeColorsFactory.h"

UDataflowSettings::UDataflowSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{
	ManagedArrayCollectionPinTypeColor = CManagedArrayCollectionPinTypeColor;
	ArrayPinTypeColor = CArrayPinTypeColor;
	BoxPinTypeColor = CBoxPinTypeColor;
}

FName UDataflowSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR

FText UDataflowSettings::GetSectionText() const
{
	return NSLOCTEXT("DataflowPlugin", "DataflowSettingsSection", "Dataflow");
}

void UDataflowSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		OnDataflowSettingsChangedDelegate.Broadcast(NodeColorsMap);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

FNodeColors UDataflowSettings::RegisterColors(const FName& Category, const FNodeColors& Colors)
{
	if (!NodeColorsMap.Contains(Category))
	{
		NodeColorsMap.Add(Category, Colors);
	}
	return NodeColorsMap[Category];
}




