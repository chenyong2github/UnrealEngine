// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerFunctionLibrary.h"
#include "MagicLeapImageTrackerModule.h"

void UMagicLeapImageTrackerFunctionLibrary::SetMaxSimultaneousTargets(int32 MaxSimultaneousTargets)
{
	GetMagicLeapImageTrackerModule().SetMaxSimultaneousTargets(MaxSimultaneousTargets);
}

int32 UMagicLeapImageTrackerFunctionLibrary::GetMaxSimultaneousTargets()
{
	return GetMagicLeapImageTrackerModule().GetMaxSimultaneousTargets();
}

void UMagicLeapImageTrackerFunctionLibrary::EnableImageTracking(bool bEnable)
{
	GetMagicLeapImageTrackerModule().SetImageTrackerEnabled(bEnable);
}

bool UMagicLeapImageTrackerFunctionLibrary::IsImageTrackingEnabled()
{
	return GetMagicLeapImageTrackerModule().GetImageTrackerEnabled();
}
