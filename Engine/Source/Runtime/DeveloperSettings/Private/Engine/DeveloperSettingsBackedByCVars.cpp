// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/DeveloperSettingsBackedByCVars.h"

UDeveloperSettingsBackedByCVars::UDeveloperSettingsBackedByCVars(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDeveloperSettingsBackedByCVars::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (IsTemplate())
	{
		ImportConsoleVariableValues();
	}
#endif
}

#if WITH_EDITOR
void UDeveloperSettingsBackedByCVars::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		ExportValuesToConsoleVariables(PropertyChangedEvent.Property);
	}
}
#endif
