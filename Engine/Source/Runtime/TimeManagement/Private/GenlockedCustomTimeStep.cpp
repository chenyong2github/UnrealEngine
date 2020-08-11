// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenlockedCustomTimeStep.h"
#include "Misc/App.h"

void UGenlockedCustomTimeStep::UpdateAppTimes(const double& TimeBeforeSync, const double& TimeAfterSync) const
{
	// Use fixed delta time to update FApp times.

	double ActualDeltaTime;
	{
		// Multiply sync time by valid SyncCountDelta to know ActualDeltaTime
		if (IsLastSyncDataValid() && (GetLastSyncCountDelta() > 0))
		{
			ActualDeltaTime = GetLastSyncCountDelta() * GetSyncRate().AsInterval();
		}
		else
		{
			// optimistic default
			ActualDeltaTime = GetFixedFrameRate().AsInterval();
		}
	}

	FApp::SetCurrentTime(TimeAfterSync);
	FApp::SetIdleTime((TimeAfterSync - TimeBeforeSync) - (ActualDeltaTime - GetFixedFrameRate().AsInterval()));
	FApp::SetDeltaTime(ActualDeltaTime);
}
