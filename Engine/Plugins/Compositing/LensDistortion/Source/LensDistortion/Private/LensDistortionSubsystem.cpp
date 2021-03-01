// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionSubsystem.h"

#include "Engine/TimecodeProvider.h"



#define LOCTEXT_NAMESPACE "LensDistortionSubsystem"



ULensFile* ULensDistortionSubsystem::GetDefaultLensFile() const
{
	return DefaultLensFile;
}

void ULensDistortionSubsystem::SetDefaultLensFile(ULensFile* NewDefaultLensFile)
{
	//Todo : Add callbacks when default lens file changes
	DefaultLensFile = NewDefaultLensFile;
}

ULensFile* ULensDistortionSubsystem::GetLensFile(const FLensFilePicker& Picker) const
{
	ULensFile* ReturnedLens = nullptr;

	if (Picker.bOverrideDefaultLensFile)
	{
		ReturnedLens = Picker.LensFile;
	}
	else
	{
		ReturnedLens = GetDefaultLensFile();
	}

	return ReturnedLens;
}

#undef LOCTEXT_NAMESPACE


