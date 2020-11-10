// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster/DisplayClusterClusterEventHandler.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterClusterEventHandler::FDisplayClusterClusterEventHandler()
{
	ListenerDelegate = FOnClusterEventJsonListener::CreateRaw(this, &FDisplayClusterClusterEventHandler::HandleClusterEvent);
}

void FDisplayClusterClusterEventHandler::HandleClusterEvent(const FDisplayClusterClusterEventJson& InEvent)
{
	// Filter system events only
	if (InEvent.bIsSystemEvent)
	{
		// Filter those events we're interested in
		if (InEvent.Category.Equals(DisplayClusterStrings::cluster_events::EventCategory, ESearchCase::IgnoreCase) &&
			InEvent.Type.Equals(DisplayClusterStrings::cluster_events::EventType, ESearchCase::IgnoreCase))
		{
			// QUIT event
			if (InEvent.Name.Equals(DisplayClusterStrings::cluster_events::EvtQuitName, ESearchCase::IgnoreCase))
			{
				FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("QUIT requested on a system cluster event"));
			}
		}
	}
}
