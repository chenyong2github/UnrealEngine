// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_APPLICATION_CORE

#include "TestCommon/ApplicationCoreUtilities.h"

void InitOutputDevicesAppCore()
{
	GError = FPlatformApplicationMisc::GetErrorOutputDevice();
	GWarn = FPlatformApplicationMisc::GetFeedbackContext();
}

#endif // WITH_APPLICATION_CORE
