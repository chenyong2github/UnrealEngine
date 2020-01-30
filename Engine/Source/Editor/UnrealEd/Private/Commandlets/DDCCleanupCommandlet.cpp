// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/DDCCleanupCommandlet.h"
#include "DDCCleanup.h"
#include "HAL/PlatformProcess.h"

int32 UDDCCleanupCommandlet::Main(const FString& Params)
{
	if (FDDCCleanup::Get())
	{
		FDDCCleanup::GetNoInit()->WaitBetweenDeletes(false);
		do
		{			
			// Cleanup works from its own thread so just wait until it's done and flush logs once in a while
			FPlatformProcess::SleepNoStats(1.0f);
			GLog->Flush();
		} while (FDDCCleanup::GetNoInit() && !FDDCCleanup::GetNoInit()->IsFinished());
	}

	return 0;
}
