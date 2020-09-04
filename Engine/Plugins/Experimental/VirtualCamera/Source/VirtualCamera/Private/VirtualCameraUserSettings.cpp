// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraUserSettings.h"

float UVirtualCameraUserSettings::GetFocusInterpSpeed()
{
	return FocusInterpSpeed;
}

void UVirtualCameraUserSettings::SetFocusInterpSpeed(const float InFocusInterpSpeed)
{
	FocusInterpSpeed = InFocusInterpSpeed;
	SaveConfig();
}

float UVirtualCameraUserSettings::GetJoysticksSpeed()
{
	return JoysticksSpeed;
}

void UVirtualCameraUserSettings::SetJoysticksSpeed(const float InJoysticksSpeed)
{
	JoysticksSpeed = InJoysticksSpeed;
	SaveConfig();
}

bool UVirtualCameraUserSettings::IsMapGrayscle()
{
	return bIsMapGrayscale;
}

void UVirtualCameraUserSettings::SetIsMapGrayscle(const bool bInIsMapGrayscle)
{
	bIsMapGrayscale = bInIsMapGrayscle;
	SaveConfig();
}

bool UVirtualCameraUserSettings::GetShouldOverrideCameraSettingsOnTeleport()
{
	return bOverrideCameraSettingsOnTeleportToScreenshot;
}

void UVirtualCameraUserSettings::SetShouldOverrideCameraSettingsOnTeleport(const bool bInOverrideCameraSettings)
{
	bOverrideCameraSettingsOnTeleportToScreenshot = bInOverrideCameraSettings;
	SaveConfig();
}

FString UVirtualCameraUserSettings::GetSavedVirtualCameraFilmbackPresetName()
{
	return GetDefault<UVirtualCameraUserSettings>()->VirtualCameraFilmback;
}

void UVirtualCameraUserSettings::SetSavedVirtualCameraFilmbackPresetName(const FString& InFilmback)
{
	VirtualCameraFilmback = InFilmback;
	SaveConfig();
}

bool UVirtualCameraUserSettings::GetShouldDisplayFilmLeader()
{
	return bDisplayFilmLeader;
}

void UVirtualCameraUserSettings::SetShouldDisplayFilmLeader(const bool bInDisplayFilmLeader)
{
	bDisplayFilmLeader = bInDisplayFilmLeader;
	SaveConfig();
}

bool UVirtualCameraUserSettings::GetTeleportOnStart()
{
    return bTeleportOnStart;
}

void UVirtualCameraUserSettings::SetTeleportOnStart(const bool bInTeleportOnStart)
{
	bTeleportOnStart = bInTeleportOnStart;
	SaveConfig();
}
