// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeFixtureSettingsDetails.h"

#include "Library/DMXEntityFixtureType.h"

#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"


#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeFixtureSettingsDetails"

TSharedRef<IDetailCustomization> FDMXEntityFixtureTypeFixtureSettingsDetails::MakeInstance(TWeakPtr<FDMXEditor> InDMXEditorPtr)
{
	return MakeShared<FDMXEntityFixtureTypeFixtureSettingsDetails>(InDMXEditorPtr);
}

void FDMXEntityFixtureTypeFixtureSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
};

#undef LOCTEXT_NAMESPACE
