// Copyright Epic Games, Inc. All Rights Reserved.
#include "TimeOfDayActorDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "LevelSequenceActor.h"

#define LOCTEXT_NAMESPACE "TimeOfDayActorDetails"

TSharedRef<IDetailCustomization> FTimeOfDayActorDetails::MakeInstance()
{
	return MakeShared<FTimeOfDayActorDetails>();
}

void FTimeOfDayActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	DetailLayout.HideProperty("DefaultComponents");
	
	// Hide level level sequence actor stuff
	DetailLayout.HideCategory("Playback");
	DetailLayout.HideCategory("BurnInOptions");
	DetailLayout.HideCategory("Aspect Ratio");
	DetailLayout.HideCategory("Cinematic");
	DetailLayout.HideCategory("InstanceData");
	DetailLayout.HideCategory("Tick");

	const TSharedPtr<IPropertyHandle> LevelSequenceProp = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(ALevelSequenceActor, LevelSequenceAsset), ALevelSequenceActor::StaticClass());
	if (IDetailPropertyRow* DetailRow = DetailLayout.EditDefaultProperty(LevelSequenceProp))
	{
		DetailRow->DisplayName(LOCTEXT("EnvironmentPreset", "Environment Preset"));
	}
}


#undef LOCTEXT_NAMESPACE
