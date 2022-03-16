// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintHeaderViewSettings.h"

UBlueprintHeaderViewSettings::UBlueprintHeaderViewSettings()
{
	
}

FName UBlueprintHeaderViewSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FText UBlueprintHeaderViewSettings::GetSectionText() const
{
	return NSLOCTEXT("BlueprintHeaderViewSettings", "HeaderViewSectionText", "Blueprint Header View");
}

FName UBlueprintHeaderViewSettings::GetSectionName() const
{
	return TEXT("Blueprint Header View");
}
