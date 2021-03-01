// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionSettings.h"


FName ULensDistortionSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText ULensDistortionSettings::GetSectionText() const
{
	return NSLOCTEXT("LensDistortionPlugin", "LensDistortionSettingsSection", "Lens Distortion");
}

#endif

ULensFile* ULensDistortionSettings::GetStartupLensFile() const
{
	return StartupLensFile.LoadSynchronous();
}

FName ULensDistortionEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText ULensDistortionEditorSettings::GetSectionText() const
{
	return NSLOCTEXT("LensDistortionEditorPlugin", "LensDistortionEditorSettingsSection", "Lens Distortion");
}
#endif

ULensFile* ULensDistortionEditorSettings::GetUserLensFile() const
{
#if WITH_EDITOR
	return UserLensFile.LoadSynchronous();
#else
	return nullptr;
#endif
}

void ULensDistortionEditorSettings::SetUserLensFile(ULensFile* InLensFile)
{
#if WITH_EDITOR
	UserLensFile = InLensFile;
	SaveConfig();
#endif
}
