// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeSettingsDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "Runtime/Landscape/Public/LandscapeSettings.h"
#include "Settings/EditorExperimentalSettings.h"
//#include "Editor.h"

FLandscapeSettingsDetails::FLandscapeSettingsDetails()
{
}


TSharedRef<IDetailCustomization> FLandscapeSettingsDetails::MakeInstance()
{
	return MakeShareable( new FLandscapeSettingsDetails);
}

void FLandscapeSettingsDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);
	DetailLayoutBuilder = &DetailBuilder;

	GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().AddSP(this, &FLandscapeSettingsDetails::OnEditorExperimentalSettingsChanged);

	if (EditingObjects.Num() == 1)
	{
		ULandscapeSettings* Settings = Cast<ULandscapeSettings>(EditingObjects[0].Get());

		if (Settings != nullptr && !GetMutableDefault<UEditorExperimentalSettings>()->bLandscapeLayerSystem)
		{
			TSharedRef<IPropertyHandle> MaxNumberOfLayersProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULandscapeSettings, MaxNumberOfLayers));
			DetailBuilder.HideProperty(MaxNumberOfLayersProperty);
		}
	}
}

void FLandscapeSettingsDetails::OnEditorExperimentalSettingsChanged(FName PropertyName)
{
	if (DetailLayoutBuilder && PropertyName == TEXT("bLandscapeLayerSystem"))
	{
		DetailLayoutBuilder->ForceRefreshDetails();
	}
}