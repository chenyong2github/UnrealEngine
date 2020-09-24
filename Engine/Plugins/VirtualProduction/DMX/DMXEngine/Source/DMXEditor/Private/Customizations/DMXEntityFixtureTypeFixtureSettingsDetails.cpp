// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeFixtureSettingsDetails.h"

#include "Library/DMXEntityFixtureType.h"

#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"


#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeFixtureSettingsDetails"

void FDMXEntityFixtureTypeFixtureSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
};

#undef LOCTEXT_NAMESPACE
