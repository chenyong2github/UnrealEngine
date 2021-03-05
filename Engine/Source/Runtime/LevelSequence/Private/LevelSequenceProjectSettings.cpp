// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceProjectSettings.h"

ULevelSequenceProjectSettings::ULevelSequenceProjectSettings()
	: bDefaultLockEngineToDisplayRate(false)
	, DefaultDisplayRate("30fps")
	, DefaultTickResolution("24000fps")
	, DefaultClockSource(EUpdateClockSource::Tick)
{ }


void ULevelSequenceProjectSettings::PostInitProperties()
{
	Super::PostInitProperties(); 

#if WITH_EDITOR
	if(IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif
}

#if WITH_EDITOR

void ULevelSequenceProjectSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if(PropertyChangedEvent.Property)
	{
		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);
	}
}

#endif

