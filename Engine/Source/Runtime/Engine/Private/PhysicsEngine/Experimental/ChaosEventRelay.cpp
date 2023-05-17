// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/ChaosEventRelay.h"

UChaosEventRelay::UChaosEventRelay()
{
}

void UChaosEventRelay::DispatchPhysicsBreakEvents(const TArray<FBreakChaosEvent>& BreakEvents)
{
	if (OnBreakEvent.IsBound())
	{
		OnBreakEvent.Broadcast(BreakEvents);
	}
}