// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODLogic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void FMassLODBaseLogic::CacheViewerInformation(TConstArrayView<FViewerInfo> Viewers, const bool bLocalViewersOnly)
{
	NumOfViewers = Viewers.Num();
	check(NumOfViewers <= UE::MassLOD::MaxNumOfViewers);

	// Cache viewer info
	for (int ViewerIdx = 0; ViewerIdx < NumOfViewers; ++ViewerIdx)
	{
		const FViewerInfo& Viewer = Viewers[ViewerIdx];
		const APlayerController* PlayerController = Viewer.PlayerController;
		const FMassViewerHandle ViewerHandle = Viewer.bEnabled && (!bLocalViewersOnly || Viewer.IsLocal()) ? Viewer.Handle : FMassViewerHandle();

		// Check if it is the same client as before
		bClearViewerData[ViewerIdx] = ViewersHandles[ViewerIdx] != ViewerHandle;
		ViewersHandles[ViewerIdx] = ViewerHandle;

		if (ViewerHandle.IsValid())
		{
			ViewersLocation[ViewerIdx] = Viewer.Location;
			ViewersDirection[ViewerIdx] = Viewer.Direction;
		}
	}
}